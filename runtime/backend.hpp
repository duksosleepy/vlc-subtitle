#pragma once

#include "runtime.h"

#include <memory>

class RuntimeBackend {
  public:
    virtual ~RuntimeBackend() = default;

    virtual bool push(const float *interleaved, size_t frames, uint32_t channels,
                      uint32_t sample_rate, int64_t pts_us) = 0;
    virtual void flush() = 0;
};

std::unique_ptr<RuntimeBackend> create_whisper_backend(const subtitle_runtime_config_t &config,
                                                       subtitle_runtime_result_cb result_cb,
                                                       subtitle_runtime_status_cb status_cb,
                                                       void *opaque);

#ifdef VLC_SUBTITLE_HAVE_VOXTRAL
std::unique_ptr<RuntimeBackend> create_voxtral_backend(const subtitle_runtime_config_t &config,
                                                       subtitle_runtime_result_cb result_cb,
                                                       subtitle_runtime_status_cb status_cb,
                                                       void *opaque);
#endif

#ifdef VLC_SUBTITLE_HAVE_PARAKEET
std::unique_ptr<RuntimeBackend> create_parakeet_backend(const subtitle_runtime_config_t &config,
                                                        subtitle_runtime_result_cb result_cb,
                                                        subtitle_runtime_status_cb status_cb,
                                                        void *opaque);
#endif

#ifdef VLC_SUBTITLE_HAVE_MOONSHINE
std::unique_ptr<RuntimeBackend> create_moonshine_backend(const subtitle_runtime_config_t &config,
                                                         subtitle_runtime_result_cb result_cb,
                                                         subtitle_runtime_status_cb status_cb,
                                                         void *opaque);
#endif
