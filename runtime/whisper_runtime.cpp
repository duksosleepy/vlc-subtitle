#include "audio_converter.hpp"
#include "backend.hpp"

#include <whisper.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

static_assert(WHISPER_SAMPLE_RATE == kRuntimeSampleRate);
constexpr int kWhisperRate = kRuntimeSampleRate;
constexpr int64_t kNoPts = kRuntimeNoPts;
constexpr int64_t kDiscontinuityUs = 500000;
constexpr size_t kMaximumBacklogSeconds = 60;

std::string trim_text(const char *value) {
    if (value == nullptr)
        return {};

    std::string text(value);
    const auto first = std::find_if_not(text.begin(), text.end(),
                                        [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
                          return std::isspace(ch) != 0;
                      }).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

bool is_supported_model(const char *model) {
    static const char *const models[] = {
        "tiny.en",   "tiny",   "base.en",  "base",     "small.en", "small",
        "medium.en", "medium", "large-v1", "large-v2", "large-v3", "large-v3-turbo",
    };
    if (model == nullptr)
        return false;
    for (const char *candidate : models)
        if (std::string(model) == candidate)
            return true;
    return false;
}

} // namespace

class WhisperBackend final : public RuntimeBackend {
  public:
    std::string model_path;
    std::string language;
    int threads = 1;
    size_t chunk_samples = 0;
    bool translate = false;
    bool use_gpu = false;

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

    ~WhisperBackend() override {
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
              int64_t pts_us) override;
    void flush() override;

    void status(const char *state, const char *message) const {
        if (status_cb != nullptr)
            status_cb(opaque, state, message);
    }

    void transcribe(whisper_context *context, const std::vector<float> &audio, int64_t start_us) {
        whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.n_threads = threads;
        params.translate = translate;
        params.no_context = true;
        params.no_timestamps = false;
        params.single_segment = false;
        params.print_special = false;
        params.print_progress = false;
        params.print_realtime = false;
        params.print_timestamps = false;
        params.language = language.empty() || language == "auto" ? nullptr : language.c_str();

        if (whisper_full(context, params, audio.data(), static_cast<int>(audio.size())) != 0) {
            status("error", "Whisper inference failed");
            return;
        }

        const int segments = whisper_full_n_segments(context);
        for (int32_t i = 0; i < segments; ++i) {
            const std::string text = trim_text(whisper_full_get_segment_text(context, i));
            if (text.empty())
                continue;

            const int64_t segment_start =
                start_us + whisper_full_get_segment_t0(context, i) * 10000;
            int64_t segment_end = start_us + whisper_full_get_segment_t1(context, i) * 10000;
            if (segment_end <= segment_start)
                segment_end = segment_start + 100000;

            if (result_cb != nullptr)
                result_cb(opaque, segment_start, segment_end, text.c_str());
        }
    }

    void run() {
        status("loading", "Loading Whisper model...");
        whisper_context_params context_params = whisper_context_default_params();
        context_params.use_gpu = use_gpu;
        whisper_context *context =
            whisper_init_from_file_with_params(model_path.c_str(), context_params);
        if (context == nullptr) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                accepting = false;
                queue.clear();
                queued_samples = 0;
            }
            status("error", "Could not load the Whisper model");
            return;
        }

        status("ready", "Whisper STT ready");

        std::vector<float> pending;
        pending.reserve(chunk_samples * 2);
        int64_t pending_start_us = kNoPts;
        int64_t expected_next_us = kNoPts;

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
                } else if (!queue.empty()) {
                    packet = std::move(queue.front());
                    queue.pop_front();
                    queued_samples -= packet.samples.size();
                }
            }

            if (!packet.samples.empty()) {
                if (packet.pts_us != kNoPts && expected_next_us != kNoPts &&
                    std::llabs(packet.pts_us - expected_next_us) > kDiscontinuityUs) {
                    pending.clear();
                    pending_start_us = kNoPts;
                }

                if (pending.empty())
                    pending_start_us = packet.pts_us;
                pending.insert(pending.end(), packet.samples.begin(), packet.samples.end());
                if (packet.pts_us != kNoPts)
                    expected_next_us = packet.pts_us + static_cast<int64_t>(packet.samples.size()) *
                                                           1000000 / kWhisperRate;

                while (pending.size() >= chunk_samples) {
                    std::vector<float> chunk(pending.begin(), pending.begin() + chunk_samples);
                    const int64_t chunk_start = pending_start_us == kNoPts ? 0 : pending_start_us;
                    transcribe(context, chunk, chunk_start);
                    pending.erase(pending.begin(), pending.begin() + chunk_samples);
                    if (pending_start_us != kNoPts)
                        pending_start_us +=
                            static_cast<int64_t>(chunk_samples) * 1000000 / kWhisperRate;
                }
            }

            if (should_reset) {
                pending.clear();
                pending_start_us = kNoPts;
                expected_next_us = kNoPts;
            }
        }

        whisper_free(context);
        {
            std::lock_guard<std::mutex> guard(mutex);
            accepting = false;
        }
        status("stopped", "Whisper STT stopped");
    }
};

std::unique_ptr<RuntimeBackend> create_whisper_backend(const subtitle_runtime_config_t &config,
                                                       subtitle_runtime_result_cb result_cb,
                                                       subtitle_runtime_status_cb status_cb,
                                                       void *opaque) {
    if (!is_supported_model(config.model_id)) {
        if (status_cb != nullptr)
            status_cb(opaque, "error", "Select a supported whisper.cpp model");
        return nullptr;
    }
    if (config.model_path == nullptr || config.model_path[0] == '\0')
        return nullptr;

    auto runtime = std::make_unique<WhisperBackend>();
    if (!runtime)
        return nullptr;

    runtime->model_path = config.model_path;
    runtime->language = config.language != nullptr ? config.language : "auto";
    runtime->threads = std::max<uint32_t>(1, config.threads);
    const uint32_t chunk_ms = std::clamp<uint32_t>(config.chunk_ms, 1000, 30000);
    runtime->chunk_samples = static_cast<size_t>(chunk_ms) * kWhisperRate / 1000;
    runtime->translate = config.translate;
    runtime->use_gpu = config.use_gpu;
    runtime->result_cb = result_cb;
    runtime->status_cb = status_cb;
    runtime->opaque = opaque;

    try {
        WhisperBackend *instance = runtime.get();
        runtime->worker = std::thread([instance] { instance->run(); });
    } catch (...) {
        return nullptr;
    }
    return runtime;
}

bool WhisperBackend::push(const float *interleaved, size_t frames, uint32_t channels,
                          uint32_t sample_rate, int64_t pts_us) {
    std::lock_guard<std::mutex> guard(mutex);
    if (stopping || !accepting)
        return false;
    RuntimeAudioPacket packet;
    packet.samples = converter.convert(interleaved, frames, channels, sample_rate);
    packet.pts_us = pts_us;
    if (packet.samples.empty() ||
        queued_samples + packet.samples.size() > kMaximumBacklogSeconds * kWhisperRate)
        return false;
    queued_samples += packet.samples.size();
    queue.push_back(std::move(packet));
    condition.notify_one();
    return true;
}

void WhisperBackend::flush() {
    {
        std::lock_guard<std::mutex> guard(mutex);
        queue.clear();
        queued_samples = 0;
        converter.reset();
        flush_requested = true;
    }
    condition.notify_one();
}
