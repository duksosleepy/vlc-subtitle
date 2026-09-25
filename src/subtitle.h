#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define DOMAIN "vlc-subtitle"
#ifndef N_
#define N_(str) (str)
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_actions.h>
#include <vlc_aout.h>
#include <vlc_configuration.h>
#include <vlc_filter.h>
#include <vlc_input.h>
#include <vlc_interface.h>
#include <vlc_mtime.h>
#include <vlc_playlist.h>
#include <vlc_subpicture.h>
#include <vlc_text_style.h>
#include <vlc_url.h>
#include <vlc_vout.h>
#include <vlc_vout_osd.h>
// clang-format on

#define SUBTITLE_ENABLE "subtitle-enable"
#define SUBTITLE_POS_X "subtitle-position-x"
#define SUBTITLE_POS_Y "subtitle-position-y"
#define SUBTITLE_FONT_SIZE "subtitle-font-size"
#define SUBTITLE_OUTPUT "subtitle-output"
#define SUBTITLE_TEXT_SOURCE "subtitle-text-source"
#define SUBTITLE_DEFAULT_TEXT "subtitle-default-text"
#define SUBTITLE_QUICK_SECONDS "subtitle-quick-seconds"
#define SUBTITLE_AUTORELOAD "subtitle-autoreload"
#define SUBTITLE_ENGINE "subtitle-engine"
#define SUBTITLE_MODEL_FILE "subtitle-model-file"
#define SUBTITLE_MODEL_ID "subtitle-model"
#define SUBTITLE_LANGUAGE "subtitle-stt-language"
#define SUBTITLE_TRANSLATE "subtitle-translate"
#define SUBTITLE_TARGET_LANGUAGE "subtitle-target-language"
#define SUBTITLE_THREADS "subtitle-threads"
#define SUBTITLE_CHUNK_MS "subtitle-chunk-ms"
#define SUBTITLE_USE_GPU "subtitle-use-gpu"
#define SUBTITLE_STATUS_DISPLAY "subtitle-status-display"
#define SUBTITLE_MODEL_STATUS_CODE "subtitle-model-status-code"
#define SUBTITLE_RUNTIME_STATE "subtitle-runtime-state"
#define SUBTITLE_RUNTIME_STATUS "subtitle-runtime-status"
#define SUBTITLE_RUNTIME_RESULT "subtitle-runtime-result"
#define SUBTITLE_FILTER_NAME "suboffline_runtime"

#define SUBTITLE_KEY_START "subtitle-key-start"
#define SUBTITLE_KEY_END "subtitle-key-end"
#define SUBTITLE_KEY_QUICK "subtitle-key-quick"
#define SUBTITLE_KEY_RELOAD "subtitle-key-reload"

#ifndef SUBTITLE_SHORTNAME
#define SUBTITLE_SHORTNAME "Subtitle Offline"
#endif

#define KEY_LEFT_BRACKET 0x0000005b
#define KEY_RIGHT_BRACKET 0x0000005d
#define KEY_BACKSLASH 0x0000005c

enum model_status {
    MODEL_STATUS_NOT_DOWNLOADED,
    MODEL_STATUS_LOADING_CACHE,
    MODEL_STATUS_DOWNLOADING_TOKENIZER,
    MODEL_STATUS_DOWNLOADING_MODEL,
    MODEL_STATUS_INITIALIZING,
    MODEL_STATUS_READY,
    MODEL_STATUS_COUNT,
};

struct subtitle_runtime_result {
    int64_t start;
    int64_t end;
    const char *text;
};

typedef struct intf_sys_t {
    vlc_mutex_t lock;
    vlc_mutex_t cue_lock;
    input_thread_t *input;
    vout_thread_t *vout;
    int spu_channel;
    int64_t cue_start;
    uint32_t cue_count;
    uint32_t text_line;
    bool subtitle_attached;
} intf_sys_t;

static inline bool is_empty_string(const char *value) {
    return value == NULL || value[0] == '\0';
}

static inline char *duplicate_or_empty(const char *value) {
    return strdup(value != NULL ? value : "");
}
