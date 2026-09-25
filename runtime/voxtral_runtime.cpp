#include "audio_converter.hpp"
#include "backend.hpp"

extern "C" {
#include <voxtral.h>
#ifdef VLC_SUBTITLE_VOXTRAL_OPENBLAS
void openblas_set_num_threads(int num_threads);
#endif
}

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace {

static_assert(VOX_SAMPLE_RATE == kRuntimeSampleRate);
constexpr size_t kMaximumBacklogSeconds = 30;
constexpr int64_t kDiscontinuityUs = 500000;
constexpr int64_t kMinimumCueUs = 100000;
constexpr const char *kModelId = "voxtral-mini-4b-realtime";

std::string trim_text(const std::string &value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
                                        [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                          return std::isspace(ch) != 0;
                      }).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

bool has_sentence_end(const std::string &text) {
    const auto end = std::find_if_not(text.rbegin(), text.rend(),
                                      [](unsigned char ch) { return std::isspace(ch) != 0; });
    return end != text.rend() && (*end == '.' || *end == '!' || *end == '?' || *end == '\n');
}

bool has_model_files(const std::string &directory) {
    std::error_code error;
    const std::filesystem::path root(directory);
    return std::filesystem::is_directory(root, error) &&
           std::filesystem::is_regular_file(root / "consolidated.safetensors", error) &&
           std::filesystem::is_regular_file(root / "params.json", error) &&
           std::filesystem::is_regular_file(root / "tekken.json", error);
}

std::string model_directory(const char *selection) {
    if (selection == nullptr || selection[0] == '\0')
        return {};

    std::error_code error;
    const std::filesystem::path path(selection);
    if (std::filesystem::is_directory(path, error))
        return path.string();
    if (path.filename() != "consolidated.safetensors")
        return {};
    const std::filesystem::path parent = path.parent_path();
    return parent.empty() ? "." : parent.string();
}

} // namespace

class VoxtralBackend final : public RuntimeBackend {
  public:
    std::string model_path;
    int chunk_ms = 2000;
    int threads = 1;

    subtitle_runtime_result_cb result_cb = nullptr;
    subtitle_runtime_status_cb status_cb = nullptr;
    void *opaque = nullptr;

    std::mutex mutex;
    std::condition_variable condition;
    std::deque<RuntimeAudioPacket> queue;
    size_t queued_samples = 0;
    RuntimeAudioConverter converter;
    bool stopping = false;
    bool accepting = true;
    bool flush_requested = false;
    std::thread worker;

    ~VoxtralBackend() override {
        {
            std::lock_guard<std::mutex> guard(mutex);
            stopping = true;
            accepting = false;
            queue.clear();
            queued_samples = 0;
        }
        condition.notify_one();
        if (worker.joinable())
            worker.join();
    }

    bool push(const float *interleaved, size_t frames, uint32_t channels, uint32_t sample_rate,
              int64_t pts_us) override {
        std::lock_guard<std::mutex> guard(mutex);
        if (stopping || !accepting)
            return false;

        RuntimeAudioPacket packet;
        packet.samples = converter.convert(interleaved, frames, channels, sample_rate);
        packet.pts_us = pts_us;
        if (packet.samples.empty() ||
            queued_samples + packet.samples.size() > kMaximumBacklogSeconds * kRuntimeSampleRate)
            return false;

        queued_samples += packet.samples.size();
        queue.push_back(std::move(packet));
        condition.notify_one();
        return true;
    }

    void flush() override {
        {
            std::lock_guard<std::mutex> guard(mutex);
            queue.clear();
            queued_samples = 0;
            converter.reset();
            flush_requested = true;
        }
        condition.notify_one();
    }

    void status(const char *state, const char *message) const {
        if (status_cb != nullptr)
            status_cb(opaque, state, message);
    }

    vox_stream_t *new_stream(vox_ctx_t *context) const {
        vox_stream_t *stream = vox_stream_init(context);
        if (stream != nullptr) {
            const float interval = std::clamp(chunk_ms / 1000.0f, 0.5f, 5.0f);
            vox_set_processing_interval(stream, interval);
            vox_stream_set_continuous(stream, 1);
        }
        return stream;
    }

    void run() {
        if (!has_model_files(model_path)) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                accepting = false;
                queue.clear();
                queued_samples = 0;
            }
            status("error", "Voxtral model directory is incomplete");
            return;
        }

        status("loading", "Loading Voxtral Mini 4B Realtime...");
#ifdef VLC_SUBTITLE_VOXTRAL_OPENBLAS
        openblas_set_num_threads(threads);
#endif
        vox_ctx_t *context = vox_load(model_path.c_str());
        if (context == nullptr) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                accepting = false;
                queue.clear();
                queued_samples = 0;
            }
            status("error", "Could not load Voxtral Mini 4B Realtime");
            return;
        }

        vox_set_delay(context, 480);
        vox_stream_t *stream = new_stream(context);
        if (stream == nullptr) {
            vox_free(context);
            {
                std::lock_guard<std::mutex> guard(mutex);
                accepting = false;
            }
            status("error", "Could not initialize the Voxtral tokenizer");
            return;
        }

        status("ready", "Voxtral Mini 4B Realtime ready");

        const int64_t cue_limit_us = static_cast<int64_t>(std::max(chunk_ms, 1000)) * 1000;
        std::string pending_text;
        int64_t pending_start_us = kRuntimeNoPts;
        int64_t last_audio_end_us = kRuntimeNoPts;
        int64_t expected_next_us = kRuntimeNoPts;
        bool failed = false;

        auto emit_pending = [&] {
            const std::string text = trim_text(pending_text);
            if (!text.empty() && result_cb != nullptr && last_audio_end_us != kRuntimeNoPts) {
                int64_t start = pending_start_us;
                if (start == kRuntimeNoPts)
                    start = std::max<int64_t>(0, last_audio_end_us - cue_limit_us);
                int64_t end = std::max(last_audio_end_us, start + kMinimumCueUs);
                result_cb(opaque, start, end, text.c_str());
            }
            pending_text.clear();
            pending_start_us = kRuntimeNoPts;
        };

        auto reset_stream = [&] {
            vox_stream_free(stream);
            stream = new_stream(context);
            pending_text.clear();
            pending_start_us = kRuntimeNoPts;
            last_audio_end_us = kRuntimeNoPts;
            expected_next_us = kRuntimeNoPts;
            return stream != nullptr;
        };

        for (;;) {
            RuntimeAudioPacket packet;
            bool should_reset = false;
            {
                std::unique_lock<std::mutex> guard(mutex);
                condition.wait(guard,
                               [this] { return stopping || flush_requested || !queue.empty(); });
                if (stopping)
                    break;
                if (flush_requested) {
                    flush_requested = false;
                    queue.clear();
                    queued_samples = 0;
                    should_reset = true;
                } else {
                    packet = std::move(queue.front());
                    queue.pop_front();
                    queued_samples -= packet.samples.size();
                }
            }

            if (should_reset && !reset_stream()) {
                status("error", "Could not reset the Voxtral stream");
                failed = true;
                break;
            }
            if (packet.samples.empty())
                continue;

            if (packet.pts_us != kRuntimeNoPts && expected_next_us != kRuntimeNoPts &&
                std::llabs(packet.pts_us - expected_next_us) > kDiscontinuityUs) {
                if (!reset_stream()) {
                    status("error", "Could not reset the Voxtral stream");
                    failed = true;
                    break;
                }
            }

            if (packet.pts_us != kRuntimeNoPts) {
                last_audio_end_us = packet.pts_us + static_cast<int64_t>(packet.samples.size()) *
                                                        1000000 / kRuntimeSampleRate;
                expected_next_us = last_audio_end_us;
            } else if (last_audio_end_us == kRuntimeNoPts)
                last_audio_end_us =
                    static_cast<int64_t>(packet.samples.size()) * 1000000 / kRuntimeSampleRate;
            else
                last_audio_end_us +=
                    static_cast<int64_t>(packet.samples.size()) * 1000000 / kRuntimeSampleRate;

            if (vox_stream_feed(stream, packet.samples.data(),
                                static_cast<int>(packet.samples.size())) != 0) {
                status("error", "Voxtral inference failed");
                failed = true;
                break;
            }

            const char *tokens[64];
            int token_count;
            while ((token_count = vox_stream_get(stream, tokens, 64)) > 0) {
                for (int32_t i = 0; i < token_count; ++i) {
                    if (tokens[i] == nullptr || tokens[i][0] == '\0')
                        continue;
                    if (pending_text.empty()) {
                        pending_start_us = std::max<int64_t>(0, last_audio_end_us - cue_limit_us);
                    }
                    pending_text += tokens[i];
                    if (has_sentence_end(pending_text))
                        emit_pending();
                }
            }

            if (!pending_text.empty() && pending_start_us != kRuntimeNoPts &&
                last_audio_end_us - pending_start_us >= cue_limit_us)
                emit_pending();
        }

        vox_stream_free(stream);
        vox_free(context);
        {
            std::lock_guard<std::mutex> guard(mutex);
            accepting = false;
        }
        if (!failed)
            status("stopped", "Voxtral STT stopped");
    }
};

std::unique_ptr<RuntimeBackend> create_voxtral_backend(const subtitle_runtime_config_t &config,
                                                       subtitle_runtime_result_cb result_cb,
                                                       subtitle_runtime_status_cb status_cb,
                                                       void *opaque) {
    if (config.model_id == nullptr || std::string(config.model_id) != kModelId ||
        config.model_path == nullptr || config.model_path[0] == '\0') {
        if (status_cb != nullptr)
            status_cb(opaque, "error",
                      "Select Voxtral Mini 4B Realtime and consolidated.safetensors");
        return nullptr;
    }

    auto runtime = std::make_unique<VoxtralBackend>();
    runtime->model_path = model_directory(config.model_path);
    if (runtime->model_path.empty()) {
        if (status_cb != nullptr)
            status_cb(opaque, "error", "The Voxtral model file must be consolidated.safetensors");
        return nullptr;
    }
    runtime->chunk_ms = std::clamp<uint32_t>(config.chunk_ms, 1000, 30000);
    runtime->threads = std::max<uint32_t>(config.threads, 1);
    runtime->result_cb = result_cb;
    runtime->status_cb = status_cb;
    runtime->opaque = opaque;

    try {
        VoxtralBackend *instance = runtime.get();
        runtime->worker = std::thread([instance] { instance->run(); });
    } catch (...) {
        return nullptr;
    }
    return runtime;
}
