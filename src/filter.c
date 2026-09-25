#include "filter.h"
#include "runtime.h"

typedef struct filter_sys_t {
    subtitle_runtime_t *runtime;
    uint32_t channels;
    uint32_t sample_rate;
    bool warned_backlog;
} filter_sys_t;

static void RuntimeResult(void *opaque, int64_t start, int64_t end, const char *text) {
    filter_t *filter = opaque;
    const struct subtitle_runtime_result result = {start, end, text};
    var_SetAddress(filter->obj.libvlc, SUBTITLE_RUNTIME_RESULT, (void *)&result);
}

static void RuntimeStatus(void *opaque, const char *state, const char *message) {
    filter_t *filter = opaque;

    if (strcmp(state, "loading") == 0) {
        var_SetInteger(filter->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE, MODEL_STATUS_INITIALIZING);
        return;
    }
    if (strcmp(state, "ready") == 0) {
        var_SetInteger(filter->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE, MODEL_STATUS_READY);
        return;
    }
    if (strcmp(state, "error") == 0) {
        var_SetInteger(filter->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE, MODEL_STATUS_NOT_DOWNLOADED);
        return;
    }

    var_SetString(filter->obj.libvlc, SUBTITLE_RUNTIME_STATE, state);
    var_SetString(filter->obj.libvlc, SUBTITLE_RUNTIME_STATUS, message);
    config_PutPsz(filter, SUBTITLE_STATUS_DISPLAY, message);
    msg_Info(filter, "STT engine [%s]: %s", state, message);
}

static block_t *ProcessRuntimeAudio(filter_t *filter, block_t *block) {
    filter_sys_t *sys = filter->p_sys;
    const int64_t pts = block->i_pts == VLC_TICK_INVALID ? INT64_MIN : block->i_pts;

    if (!subtitle_runtime_push(sys->runtime, (const float *)block->p_buffer, block->i_nb_samples,
                               sys->channels, sys->sample_rate, pts) &&
        !sys->warned_backlog) {
        msg_Warn(filter, "STT engine is not accepting audio; dropping audio");
        sys->warned_backlog = true;
    }
    return block;
}

static void FlushRuntimeAudio(filter_t *filter) {
    filter_sys_t *sys = filter->p_sys;
    subtitle_runtime_flush(sys->runtime);
    sys->warned_backlog = false;
}

void CloseRuntimeFilter(vlc_object_t *object) {
    filter_t *filter = (filter_t *)object;
    filter_sys_t *sys = filter->p_sys;
    if (sys != NULL) {
        subtitle_runtime_destroy(sys->runtime);
        free(sys);
    }
}

static const char *get_backend_for_model(const char *model_id) {
    if (model_id == NULL || model_id[0] == '\0')
        return "whisper";
    if (strncasecmp(model_id, "voxtral", 7) == 0)
        return "voxtral";
    if (strncasecmp(model_id, "parakeet", 8) == 0 || strncasecmp(model_id, "nemotron", 8) == 0)
        return "parakeet";
    if (strncasecmp(model_id, "moonshine", 9) == 0)
        return "moonshine";
    return "whisper";
}

int OpenRuntimeFilter(vlc_object_t *object) {
    filter_t *filter = (filter_t *)object;
    if (!var_InheritBool(filter, SUBTITLE_ENABLE)) {
        msg_Dbg(filter, "offline subtitles plugin is disabled");
        return VLC_EGENERIC;
    }

    char *configured_backend = var_InheritString(filter, SUBTITLE_ENGINE);
    char *model_id = var_InheritString(filter, SUBTITLE_MODEL_ID);
    char *model = var_InheritString(filter, SUBTITLE_MODEL_FILE);

    const char *effective_model = is_empty_string(model_id) ? "tiny.en" : model_id;
    const char *backend = get_backend_for_model(effective_model);
    if (!is_empty_string(configured_backend) && strcmp(effective_model, "tiny.en") == 0 &&
        strcmp(configured_backend, "whisper") != 0) {
        /* Legacy CLI flag --subtitle-engine was specified without changing model-id */
        backend = configured_backend;
        if (strcmp(backend, "voxtral") == 0)
            effective_model = "voxtral-mini-4b-realtime";
        else if (strcmp(backend, "parakeet") == 0)
            effective_model = "parakeet-tdt_ctc-110m";
        else if (strcmp(backend, "moonshine") == 0)
            effective_model = "moonshine-tiny";
    }

    msg_Info(filter, "Selected model '%s' -> using engine '%s'", effective_model, backend);

    const bool use_voxtral = strcmp(backend, "voxtral") == 0;
    const bool use_parakeet = strcmp(backend, "parakeet") == 0;
    const bool use_moonshine = strcmp(backend, "moonshine") == 0;
    if (is_empty_string(model)) {
        const char *message = use_voxtral    ? "Select the Voxtral consolidated.safetensors file"
                              : use_parakeet ? "Select a Parakeet GGUF model file"
                              : use_moonshine
                                  ? "Select tokenizer.bin from a Moonshine model directory"
                                  : "Set a local Whisper model file in preferences";
        RuntimeStatus(filter, "error", message);
        free(model);
        free(model_id);
        free(configured_backend);
        return VLC_EGENERIC;
    }

    char *language = var_InheritString(filter, SUBTITLE_LANGUAGE);
    filter_sys_t *sys = calloc(1, sizeof(*sys));
    if (sys == NULL) {
        free(configured_backend);
        free(language);
        free(model);
        free(model_id);
        return VLC_ENOMEM;
    }

    filter->fmt_in.audio.i_format = VLC_CODEC_FL32;
    filter->fmt_in.i_codec = VLC_CODEC_FL32;
    aout_FormatPrepare(&filter->fmt_in.audio);
    filter->fmt_out.audio = filter->fmt_in.audio;
    filter->fmt_out.i_codec = VLC_CODEC_FL32;

    sys->channels = (uint32_t)filter->fmt_in.audio.i_channels;
    sys->sample_rate = (uint32_t)filter->fmt_in.audio.i_rate;

    char *target_lang = var_InheritString(filter, SUBTITLE_TARGET_LANGUAGE);
    bool translate = false;
    if (!is_empty_string(target_lang) && strcmp(target_lang, "none") != 0)
        translate = true;
    else
        translate = var_InheritBool(filter, SUBTITLE_TRANSLATE);

    const subtitle_runtime_config_t config = {
        .backend = backend,
        .model_id = effective_model,
        .model_path = model,
        .language = is_empty_string(language) ? "auto" : language,
        .threads = (uint32_t)var_InheritInteger(filter, SUBTITLE_THREADS),
        .chunk_ms = (uint32_t)var_InheritInteger(filter, SUBTITLE_CHUNK_MS),
        .translate = translate,
        .use_gpu = var_InheritBool(filter, SUBTITLE_USE_GPU),
    };
    sys->runtime = subtitle_runtime_create(&config, RuntimeResult, RuntimeStatus, filter);
    free(target_lang);
    free(configured_backend);
    free(language);
    free(model);
    free(model_id);
    if (sys->runtime == NULL) {
        free(sys);
        return VLC_EGENERIC;
    }

    filter->p_sys = sys;
    filter->pf_audio_filter = ProcessRuntimeAudio;
    filter->pf_flush = FlushRuntimeAudio;
    return VLC_SUCCESS;
}

void SetAudioFilterEnabled(audio_output_t *aout, const char *name, bool enabled) {
    char *current = var_GetString(aout, "audio-filter");
    if (current == NULL)
        current = strdup("");
    if (current == NULL)
        return;

    const size_t capacity = strlen(current) + strlen(name) + 2;
    char *updated = calloc(capacity, 1);
    if (updated == NULL) {
        free(current);
        return;
    }

    bool found = false;
    const char *cursor = current;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == ':')
            cursor++;
        const char *end = cursor;
        while (*end != '\0' && *end != ' ' && *end != ':')
            end++;

        const size_t length = (size_t)(end - cursor);
        const bool matches = length == strlen(name) && strncmp(cursor, name, length) == 0;
        found |= matches;
        if (length > 0 && (enabled || !matches)) {
            if (updated[0] != '\0')
                strcat(updated, ":");
            strncat(updated, cursor, length);
        }
        cursor = end;
    }

    if (enabled && !found) {
        if (updated[0] != '\0')
            strcat(updated, ":");
        strcat(updated, name);
    }

    if (strcmp(current, updated) != 0)
        var_SetString(aout, "audio-filter", updated);
    free(updated);
    free(current);
}

void SetRuntimeFilter(input_thread_t *input, bool enabled) {
    if (input == NULL)
        return;

    audio_output_t *aout = input_GetAout(input);
    if (aout != NULL) {
        SetAudioFilterEnabled(aout, SUBTITLE_FILTER_NAME, enabled);
        vlc_object_release(aout);
    }
}
