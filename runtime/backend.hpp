#ifndef VLC_SUBTITLE_STT_BACKEND_HPP
#define VLC_SUBTITLE_STT_BACKEND_HPP

#include "runtime.h"

#include <memory>

class RuntimeBackend
{
public:
    virtual ~RuntimeBackend() = default;

    virtual bool push(const float *interleaved, size_t frames,
                      unsigned channels, unsigned sample_rate,
                      int64_t pts_us) = 0;
    virtual void flush() = 0;
};

std::unique_ptr<RuntimeBackend> create_whisper_backend(
    const subtitle_runtime_config_t &config,
    subtitle_runtime_result_cb result_cb,
    subtitle_runtime_status_cb status_cb,
    void *opaque);

#endif
