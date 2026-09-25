#include "audio_converter.hpp"
#include "backend.hpp"

#include <parakeet_capi.h>

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
#include <vector>

// These lifecycle controls are part of the pinned parakeet.cpp implementation,
// but are not yet exposed by its flat C API. Keep their use isolated here.
namespace pk {
void set_num_threads(int n);
void shutdown_backend();
} // namespace pk

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

bool is_supported_model(const char *model) {
    static const char *const models[] = {
        "parakeet-tdt_ctc-110m",
        "parakeet-ctc-0.6b",
        "parakeet-rnnt-0.6b",
        "parakeet-tdt-0.6b-v2",
        "parakeet-tdt-0.6b-v3",
        "parakeet-ctc-1.1b",
        "parakeet-rnnt-1.1b",
        "parakeet-tdt-1.1b",
        "parakeet-tdt_ctc-1.1b",
        "parakeet_realtime_eou_120m-v1",
        "nemotron-3.5-asr-streaming-0.6b",
    };
    if (model == nullptr)
        return false;
    for (const char *candidate : models)
        if (std::string(model) == candidate)
            return true;
    return false;
}

bool is_gguf_file(const std::string &path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return false;

    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".gguf";
}

class DeviceOverride {
  public:
    explicit DeviceOverride(bool force_cpu) : active(force_cpu) {
        if (!active)
            return;
        const char *current = std::getenv("PARAKEET_DEVICE");
        if (current != nullptr) {
            had_value = true;
            previous = current;
        }
        set("cpu");
    }

    ~DeviceOverride() {
        if (!active)
            return;
        if (had_value)
            set(previous.c_str());
        else
            unset();
    }

  private:
    static void set(const char *value) {
#ifdef _WIN32
        _putenv_s("PARAKEET_DEVICE", value);
#else
        setenv("PARAKEET_DEVICE", value, 1);
#endif
    }

    static void unset() {
#ifdef _WIN32
        _putenv_s("PARAKEET_DEVICE", "");
#else
        unsetenv("PARAKEET_DEVICE");
#endif
    }

    bool active = false;
    bool had_value = false;
    std::string previous;
};

} // namespace

class ParakeetBackend final : public RuntimeBackend {
  public:
    std::string model_path;
    std::string language;
    int threads = 1;
    size_t chunk_samples = 0;
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

    ~ParakeetBackend() override {
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

    bool transcribe(parakeet_ctx *context, const std::vector<float> &audio, int64_t start_us) {
        char *raw = parakeet_capi_transcribe_pcm_lang(
            context, audio.data(), static_cast<int>(audio.size()), kRuntimeSampleRate, 0,
            language.empty() ? nullptr : language.c_str());
        if (raw == nullptr) {
            std::string message = "Parakeet inference failed";
            const char *detail = parakeet_capi_last_error(context);
            if (detail != nullptr && detail[0] != '\0')
                message += ": " + std::string(detail);
            status("error", message.c_str());
            return false;
        }

        const std::string text = trim_text(raw);
        parakeet_capi_free_string(raw);
        if (text.empty())
            return true;

        int64_t end_us =
            start_us + static_cast<int64_t>(audio.size()) * 1000000 / kRuntimeSampleRate;
        if (end_us <= start_us)
            end_us = start_us + kMinimumCueUs;
        if (result_cb != nullptr)
            result_cb(opaque, start_us, end_us, text.c_str());
        return true;
    }

    void run() {
        if (!is_gguf_file(model_path)) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                accepting = false;
                queue.clear();
                queued_samples = 0;
            }
            status("error", "Select an existing Parakeet GGUF model file");
            return;
        }

        status("loading", "Loading Parakeet model...");
        pk::set_num_threads(threads);
        parakeet_ctx *context;
        {
            DeviceOverride device(!use_gpu);
            context = parakeet_capi_load(model_path.c_str());
        }
        if (context == nullptr) {
            pk::shutdown_backend();
            {
                std::lock_guard<std::mutex> guard(mutex);
                accepting = false;
                queue.clear();
                queued_samples = 0;
            }
            status("error", "Could not load the Parakeet GGUF model");
            return;
        }

        status("ready", "Parakeet STT ready");

        std::vector<float> pending;
        pending.reserve(chunk_samples * 2);
        int64_t pending_start_us = kRuntimeNoPts;
        int64_t expected_next_us = kRuntimeNoPts;
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
                pending.clear();
                pending_start_us = kRuntimeNoPts;
                expected_next_us = kRuntimeNoPts;
                continue;
            }
            if (packet.samples.empty())
                continue;

            if (packet.pts_us != kRuntimeNoPts && expected_next_us != kRuntimeNoPts &&
                std::llabs(packet.pts_us - expected_next_us) > kDiscontinuityUs) {
                pending.clear();
                pending_start_us = kRuntimeNoPts;
            }

            if (pending.empty())
                pending_start_us = packet.pts_us;
            pending.insert(pending.end(), packet.samples.begin(), packet.samples.end());
            if (packet.pts_us != kRuntimeNoPts)
                expected_next_us = packet.pts_us + static_cast<int64_t>(packet.samples.size()) *
                                                       1000000 / kRuntimeSampleRate;

            while (pending.size() >= chunk_samples) {
                std::vector<float> chunk(pending.begin(), pending.begin() + chunk_samples);
                const int64_t chunk_start =
                    pending_start_us == kRuntimeNoPts ? 0 : pending_start_us;
                if (!transcribe(context, chunk, chunk_start)) {
                    failed = true;
                    break;
                }
                pending.erase(pending.begin(), pending.begin() + chunk_samples);
                if (pending_start_us != kRuntimeNoPts)
                    pending_start_us +=
                        static_cast<int64_t>(chunk_samples) * 1000000 / kRuntimeSampleRate;
            }
            if (failed)
                break;
        }

        parakeet_capi_free(context);
        pk::shutdown_backend();
        {
            std::lock_guard<std::mutex> guard(mutex);
            accepting = false;
        }
        if (!failed)
            status("stopped", "Parakeet STT stopped");
    }
};

std::unique_ptr<RuntimeBackend> create_parakeet_backend(const subtitle_runtime_config_t &config,
                                                        subtitle_runtime_result_cb result_cb,
                                                        subtitle_runtime_status_cb status_cb,
                                                        void *opaque) {
    if (!is_supported_model(config.model_id)) {
        if (status_cb != nullptr)
            status_cb(opaque, "error", "Select a supported Parakeet model");
        return nullptr;
    }
    if (config.model_path == nullptr || config.model_path[0] == '\0')
        return nullptr;

    auto runtime = std::make_unique<ParakeetBackend>();
    runtime->model_path = config.model_path;
    runtime->language = config.language != nullptr ? config.language : "auto";
    runtime->threads = std::max<uint32_t>(1, config.threads);
    const uint32_t chunk_ms = std::clamp<uint32_t>(config.chunk_ms, 1000, 30000);
    runtime->chunk_samples = static_cast<size_t>(chunk_ms) * kRuntimeSampleRate / 1000;
    runtime->use_gpu = config.use_gpu;
    runtime->result_cb = result_cb;
    runtime->status_cb = status_cb;
    runtime->opaque = opaque;

    try {
        ParakeetBackend *instance = runtime.get();
        runtime->worker = std::thread([instance] { instance->run(); });
    } catch (...) {
        return nullptr;
    }
    return runtime;
}
