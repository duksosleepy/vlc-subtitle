#include "audio_converter.hpp"
#include "backend.hpp"

#include <moonshine-c-api.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>

namespace {

constexpr int64_t kDiscontinuityUs = 500000;
constexpr int64_t kMinimumCueUs = 100000;
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

int model_arch(const char *model) {
    if (model == nullptr)
        return -1;
    const std::string id(model);
    if (id == "moonshine-tiny")
        return MOONSHINE_MODEL_ARCH_TINY;
    if (id == "moonshine-base")
        return MOONSHINE_MODEL_ARCH_BASE;
    if (id == "moonshine-tiny-streaming")
        return MOONSHINE_MODEL_ARCH_TINY_STREAMING;
    if (id == "moonshine-base-streaming")
        return MOONSHINE_MODEL_ARCH_BASE_STREAMING;
    if (id == "moonshine-small-streaming")
        return MOONSHINE_MODEL_ARCH_SMALL_STREAMING;
    if (id == "moonshine-medium-streaming")
        return MOONSHINE_MODEL_ARCH_MEDIUM_STREAMING;
    return -1;
}

bool is_streaming_arch(int arch) {
    return arch == MOONSHINE_MODEL_ARCH_TINY_STREAMING ||
           arch == MOONSHINE_MODEL_ARCH_BASE_STREAMING ||
           arch == MOONSHINE_MODEL_ARCH_SMALL_STREAMING ||
           arch == MOONSHINE_MODEL_ARCH_MEDIUM_STREAMING;
}

std::string model_directory(const char *selection) {
    if (selection == nullptr || selection[0] == '\0')
        return {};

    std::error_code error;
    const std::filesystem::path path(selection);
    if (std::filesystem::is_directory(path, error))
        return path.string();
    if (!std::filesystem::is_regular_file(path, error))
        return {};
    const std::filesystem::path parent = path.parent_path();
    return parent.empty() ? "." : parent.string();
}

bool has_model_files(const std::string &directory, int arch) {
    std::error_code error;
    const std::filesystem::path root(directory);
    if (!std::filesystem::is_directory(root, error) ||
        !std::filesystem::is_regular_file(root / "tokenizer.bin", error))
        return false;

    if (!is_streaming_arch(arch))
        return std::filesystem::is_regular_file(root / "encoder_model.ort", error) &&
               std::filesystem::is_regular_file(root / "decoder_model_merged.ort", error);

    static const char *const files[] = {
        "frontend.ort", "encoder.ort",    "adapter.ort",
        "cross_kv.ort", "decoder_kv.ort", "streaming_config.json",
    };
    for (const char *file : files)
        if (!std::filesystem::is_regular_file(root / file, error))
            return false;
    return true;
}

} // namespace

class MoonshineBackend final : public RuntimeBackend {
  public:
    std::string model_path;
    int arch = -1;
    size_t chunk_samples = 0;

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

    ~MoonshineBackend() override {
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

    bool report_error(const char *operation, int32_t error) const {
        std::string message = operation;
        const char *detail = moonshine_error_to_string(error);
        if (detail != nullptr && detail[0] != '\0')
            message += ": " + std::string(detail);
        status("error", message.c_str());
        return false;
    }

    bool emit_completed(int32_t transcriber, int32_t stream, int64_t origin_us,
                        std::unordered_set<uint64_t> &emitted, uint32_t flags = 0) {
        transcript_t *transcript = nullptr;
        const int32_t error = moonshine_transcribe_stream(transcriber, stream, flags, &transcript);
        if (error != MOONSHINE_ERROR_NONE)
            return report_error("Moonshine inference failed", error);
        if (transcript == nullptr)
            return true;

        const int64_t base = origin_us == kRuntimeNoPts ? 0 : origin_us;
        for (uint64_t i = 0; i < transcript->line_count; ++i) {
            const transcript_line_t &line = transcript->lines[i];
            if (!line.is_complete || emitted.find(line.id) != emitted.end())
                continue;
            emitted.insert(line.id);

            const std::string text = trim_text(line.text);
            if (text.empty())
                continue;
            const int64_t start = base + static_cast<int64_t>(std::llround(
                                             static_cast<double>(line.start_time) * 1000000.0));
            int64_t end = start + static_cast<int64_t>(
                                      std::llround(static_cast<double>(line.duration) * 1000000.0));
            if (end <= start)
                end = start + kMinimumCueUs;
            if (result_cb != nullptr)
                result_cb(opaque, start, end, text.c_str());
        }
        return true;
    }

    void run() {
        if (!has_model_files(model_path, arch)) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                accepting = false;
                queue.clear();
                queued_samples = 0;
            }
            status("error", "Moonshine model directory is incomplete");
            return;
        }

        status("loading", "Loading Moonshine model...");
        const std::string interval =
            std::to_string(static_cast<double>(chunk_samples) / kRuntimeSampleRate);
        const moonshine_option_t options[] = {
            {"identify_speakers", "false"},
            {"return_audio_data", "false"},
            {"transcription_interval", interval.c_str()},
        };
        const int32_t transcriber = moonshine_load_transcriber_from_files(
            model_path.c_str(), static_cast<uint32_t>(arch), options,
            sizeof(options) / sizeof(options[0]), MOONSHINE_HEADER_VERSION);
        if (transcriber < 0) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                accepting = false;
                queue.clear();
                queued_samples = 0;
            }
            report_error("Could not load the Moonshine model", transcriber);
            return;
        }

        int32_t stream = -1;
        std::unordered_set<uint64_t> emitted;
        int64_t stream_origin_us = kRuntimeNoPts;
        int64_t expected_next_us = kRuntimeNoPts;
        size_t stream_samples = 0;
        size_t samples_since_update = 0;

        auto close_stream = [&](bool finalize) {
            if (stream < 0)
                return true;
            bool ok = true;
            const int32_t stop_error = moonshine_stop_stream(transcriber, stream);
            if (stop_error != MOONSHINE_ERROR_NONE)
                ok = report_error("Could not stop the Moonshine stream", stop_error);
            if (ok && finalize)
                ok = emit_completed(transcriber, stream, stream_origin_us, emitted,
                                    MOONSHINE_FLAG_FORCE_UPDATE);
            const int32_t free_error = moonshine_free_stream(transcriber, stream);
            if (free_error != MOONSHINE_ERROR_NONE && ok)
                ok = report_error("Could not free the Moonshine stream", free_error);
            stream = -1;
            return ok;
        };

        auto open_stream = [&] {
            stream = moonshine_create_stream(transcriber, 0);
            if (stream < 0)
                return report_error("Could not create the Moonshine stream", stream);
            const int32_t error = moonshine_start_stream(transcriber, stream);
            if (error != MOONSHINE_ERROR_NONE) {
                moonshine_free_stream(transcriber, stream);
                stream = -1;
                return report_error("Could not start the Moonshine stream", error);
            }
            emitted.clear();
            stream_origin_us = kRuntimeNoPts;
            expected_next_us = kRuntimeNoPts;
            stream_samples = 0;
            samples_since_update = 0;
            return true;
        };

        if (!open_stream()) {
            moonshine_free_transcriber(transcriber);
            std::lock_guard<std::mutex> guard(mutex);
            accepting = false;
            return;
        }
        status("ready", "Moonshine STT ready (CPU)");

        bool failed = false;
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

            if (should_reset) {
                if (!close_stream(false) || !open_stream()) {
                    failed = true;
                    break;
                }
                continue;
            }
            if (packet.samples.empty())
                continue;

            if (packet.pts_us != kRuntimeNoPts && expected_next_us != kRuntimeNoPts &&
                std::llabs(packet.pts_us - expected_next_us) > kDiscontinuityUs) {
                if (!close_stream(false) || !open_stream()) {
                    failed = true;
                    break;
                }
            }

            if (stream_origin_us == kRuntimeNoPts && packet.pts_us != kRuntimeNoPts) {
                stream_origin_us = packet.pts_us - static_cast<int64_t>(stream_samples) * 1000000 /
                                                       kRuntimeSampleRate;
            }
            if (packet.pts_us != kRuntimeNoPts)
                expected_next_us = packet.pts_us + static_cast<int64_t>(packet.samples.size()) *
                                                       1000000 / kRuntimeSampleRate;

            const int32_t add_error = moonshine_transcribe_add_audio_to_stream(
                transcriber, stream, packet.samples.data(), packet.samples.size(),
                kRuntimeSampleRate, 0);
            if (add_error != MOONSHINE_ERROR_NONE) {
                report_error("Could not feed the Moonshine stream", add_error);
                failed = true;
                break;
            }
            stream_samples += packet.samples.size();
            samples_since_update += packet.samples.size();
            if (samples_since_update >= chunk_samples) {
                samples_since_update = 0;
                if (!emit_completed(transcriber, stream, stream_origin_us, emitted)) {
                    failed = true;
                    break;
                }
            }
        }

        if (!close_stream(!failed))
            failed = true;
        moonshine_free_transcriber(transcriber);
        {
            std::lock_guard<std::mutex> guard(mutex);
            accepting = false;
        }
        if (!failed)
            status("stopped", "Moonshine STT stopped");
    }
};

std::unique_ptr<RuntimeBackend> create_moonshine_backend(const subtitle_runtime_config_t &config,
                                                         subtitle_runtime_result_cb result_cb,
                                                         subtitle_runtime_status_cb status_cb,
                                                         void *opaque) {
    const int arch = model_arch(config.model_id);
    if (arch < 0) {
        if (status_cb != nullptr)
            status_cb(opaque, "error", "Select a supported Moonshine model");
        return nullptr;
    }

    const std::string directory = model_directory(config.model_path);
    if (directory.empty()) {
        if (status_cb != nullptr)
            status_cb(opaque, "error", "Select a file from the Moonshine model directory");
        return nullptr;
    }

    auto runtime = std::make_unique<MoonshineBackend>();
    runtime->model_path = directory;
    runtime->arch = arch;
    const uint32_t chunk_ms = std::clamp<uint32_t>(config.chunk_ms, 1000, 30000);
    runtime->chunk_samples = static_cast<size_t>(chunk_ms) * kRuntimeSampleRate / 1000;
    runtime->result_cb = result_cb;
    runtime->status_cb = status_cb;
    runtime->opaque = opaque;

    try {
        MoonshineBackend *instance = runtime.get();
        runtime->worker = std::thread([instance] { instance->run(); });
    } catch (...) {
        return nullptr;
    }
    return runtime;
}
