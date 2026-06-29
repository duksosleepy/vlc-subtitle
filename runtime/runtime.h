#ifndef VLC_SUBTITLE_RUNTIME_H
#define VLC_SUBTITLE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct subtitle_runtime subtitle_runtime_t;

typedef struct subtitle_runtime_config
{
    const char *backend;
    const char *model_path;
    const char *language;
    int threads;
    int chunk_ms;
    bool translate;
    bool use_gpu;
} subtitle_runtime_config_t;

typedef void (*subtitle_runtime_result_cb)(void *opaque, int64_t start_us,
                                           int64_t end_us, const char *text);
typedef void (*subtitle_runtime_status_cb)(void *opaque, const char *state,
                                           const char *message);

subtitle_runtime_t *subtitle_runtime_create(
    const subtitle_runtime_config_t *config,
    subtitle_runtime_result_cb result_cb,
    subtitle_runtime_status_cb status_cb,
    void *opaque);

bool subtitle_runtime_push(subtitle_runtime_t *runtime,
                           const float *interleaved,
                           size_t frames,
                           unsigned channels,
                           unsigned sample_rate,
                           int64_t pts_us);

void subtitle_runtime_flush(subtitle_runtime_t *runtime);
void subtitle_runtime_destroy(subtitle_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
