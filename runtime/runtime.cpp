#include "runtime.h"

#include "backend.hpp"

#include <cstring>
#include <memory>
#include <new>

struct subtitle_runtime
{
    std::unique_ptr<RuntimeBackend> backend;
};

extern "C" subtitle_runtime_t *subtitle_runtime_create(
    const subtitle_runtime_config_t *config,
    subtitle_runtime_result_cb result_cb,
    subtitle_runtime_status_cb status_cb,
    void *opaque)
{
    try
    {
        if (config == nullptr || config->backend == nullptr ||
            config->model_path == nullptr || config->model_path[0] == '\0')
            return nullptr;

        std::unique_ptr<RuntimeBackend> backend;
        if (std::strcmp(config->backend, "whisper") == 0)
            backend = create_whisper_backend(*config, result_cb, status_cb,
                                             opaque);
#ifdef VLC_SUBTITLE_HAVE_VOXTRAL
        else if (std::strcmp(config->backend, "voxtral") == 0)
            backend = create_voxtral_backend(*config, result_cb, status_cb,
                                             opaque);
#else
        else if (std::strcmp(config->backend, "voxtral") == 0)
        {
            if (status_cb != nullptr)
                status_cb(opaque, "error",
                          "Voxtral is unavailable in this build");
            return nullptr;
        }
#endif
        else if (std::strcmp(config->backend, "parakeet") == 0)
        {
#ifdef VLC_SUBTITLE_HAVE_PARAKEET
            backend = create_parakeet_backend(*config, result_cb, status_cb,
                                              opaque);
#else
            if (status_cb != nullptr)
                status_cb(opaque, "error",
                          "Parakeet is unavailable in this build");
            return nullptr;
#endif
        }
        else if (std::strcmp(config->backend, "moonshine") == 0)
        {
#ifdef VLC_SUBTITLE_HAVE_MOONSHINE
            backend = create_moonshine_backend(*config, result_cb, status_cb,
                                               opaque);
#else
            if (status_cb != nullptr)
                status_cb(opaque, "error",
                          "Moonshine is unavailable in this build");
            return nullptr;
#endif
        }
        else
        {
            if (status_cb != nullptr)
                status_cb(opaque, "error", "Unknown STT engine");
            return nullptr;
        }

        if (!backend)
            return nullptr;

        auto *runtime = new (std::nothrow) subtitle_runtime;
        if (runtime == nullptr)
            return nullptr;
        runtime->backend = std::move(backend);
        return runtime;
    }
    catch (...)
    {
        if (status_cb != nullptr)
            status_cb(opaque, "error", "Could not create the STT engine");
        return nullptr;
    }
}

extern "C" bool subtitle_runtime_push(subtitle_runtime_t *runtime,
                                       const float *interleaved,
                                       size_t frames,
                                       unsigned channels,
                                       unsigned sample_rate,
                                       int64_t pts_us)
{
    if (runtime == nullptr)
        return false;
    try
    {
        return runtime->backend->push(interleaved, frames, channels,
                                      sample_rate, pts_us);
    }
    catch (...)
    {
        return false;
    }
}

extern "C" void subtitle_runtime_flush(subtitle_runtime_t *runtime)
{
    if (runtime != nullptr)
    {
        try
        {
            runtime->backend->flush();
        }
        catch (...)
        {
        }
    }
}

extern "C" void subtitle_runtime_destroy(subtitle_runtime_t *runtime)
{
    delete runtime;
}
