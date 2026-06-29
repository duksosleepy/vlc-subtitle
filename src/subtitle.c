#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define DOMAIN "vlc-subtitle"
#define _(str) dgettext(DOMAIN, str)
#define N_(str) (str)

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_actions.h>
#include <vlc_aout.h>
#include <vlc_configuration.h>
#include <vlc_filter.h>
#include <vlc_input.h>
#include <vlc_interface.h>
#include <vlc_mtime.h>
#include <vlc_playlist.h>
#include <vlc_plugin.h>
#include <vlc_url.h>

#include "runtime.h"

#define SUBTITLE_OUTPUT "subtitle-output"
#define SUBTITLE_TEXT_SOURCE "subtitle-text-source"
#define SUBTITLE_DEFAULT_TEXT "subtitle-default-text"
#define SUBTITLE_QUICK_SECONDS "subtitle-quick-seconds"
#define SUBTITLE_AUTORELOAD "subtitle-autoreload"
#define SUBTITLE_BACKEND "subtitle-stt-runtime"
#define SUBTITLE_MODEL "subtitle-stt-model"
#define SUBTITLE_LANGUAGE "subtitle-stt-language"
#define SUBTITLE_TRANSLATE "subtitle-translate"
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
# define SUBTITLE_SHORTNAME "Subtitle Offline"
#endif

#define KEY_LEFT_BRACKET 0x0000005b
#define KEY_RIGHT_BRACKET 0x0000005d
#define KEY_BACKSLASH 0x0000005c

static int Open(vlc_object_t *);
static void Close(vlc_object_t *);
static int OpenRuntimeFilter(vlc_object_t *);
static void CloseRuntimeFilter(vlc_object_t *);

static int KeyboardEvent(vlc_object_t *, char const *, vlc_value_t,
                         vlc_value_t, void *);
static int PlaylistEvent(vlc_object_t *, char const *, vlc_value_t,
                         vlc_value_t, void *);
static int InputEvent(vlc_object_t *, char const *, vlc_value_t,
                      vlc_value_t, void *);
static int RuntimeResultEvent(vlc_object_t *, char const *, vlc_value_t,
                              vlc_value_t, void *);
static int ModelStatusEvent(vlc_object_t *, char const *, vlc_value_t,
                            vlc_value_t, void *);

static void ChangeInput(intf_thread_t *, input_thread_t *);
static void ChangeVout(intf_thread_t *, vout_thread_t *);

enum model_status
{
    MODEL_STATUS_NOT_DOWNLOADED,
    MODEL_STATUS_LOADING_CACHE,
    MODEL_STATUS_DOWNLOADING_TOKENIZER,
    MODEL_STATUS_DOWNLOADING_MODEL,
    MODEL_STATUS_INITIALIZING,
    MODEL_STATUS_READY,
    MODEL_STATUS_COUNT,
};

static void SetModelStatus(intf_thread_t *, enum model_status);

static const char *const language_values[] = {
    "auto", "en", "vi", "zh", "ja", "ko", "es", "fr", "de", "it",
    "pt", "ru", "ar", "hi", "th", "id", "tr", "uk", "nl", "pl",
};

static const char *const language_labels[] = {
    N_("Auto detect"), N_("English"), N_("Vietnamese"), N_("Chinese"),
    N_("Japanese"), N_("Korean"), N_("Spanish"), N_("French"),
    N_("German"), N_("Italian"), N_("Portuguese"), N_("Russian"),
    N_("Arabic"), N_("Hindi"), N_("Thai"), N_("Indonesian"),
    N_("Turkish"), N_("Ukrainian"), N_("Dutch"), N_("Polish"),
};

static const char *const runtime_values[] = {
    "whisper",
};

static const char *const runtime_labels[] = {
    N_("whisper.cpp"),
};

vlc_module_begin()
    set_text_domain(DOMAIN)
    set_shortname(N_(SUBTITLE_SHORTNAME))
    set_description(N_("Offline speech-to-text subtitles with whisper.cpp"))
    set_capability("interface", 0)
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)

    set_section(N_("Settings · Model"), NULL)
    add_string(SUBTITLE_BACKEND, "whisper",
               N_("STT runtime"),
               N_("Speech-to-text inference backend."), false)
        change_string_list(runtime_values, runtime_labels)
    add_loadfile(SUBTITLE_MODEL, "",
                 N_("STT model file"),
                 N_("Local model for the selected runtime. whisper.cpp uses "
                    "GGML/GGUF models such as ggml-base.bin."),
                 false)
    add_integer_with_range(SUBTITLE_THREADS, 4, 1, 32,
                           N_("Inference threads"),
                           N_("CPU threads used by whisper.cpp."), false)
    add_integer_with_range(SUBTITLE_CHUNK_MS, 5000, 1000, 30000,
                           N_("Transcription chunk length (ms)"),
                           N_("Audio accumulated before each STT inference. "
                              "Shorter chunks reduce latency but may reduce "
                              "accuracy."), false)
    add_bool(SUBTITLE_USE_GPU, true,
             N_("Use GPU acceleration"),
             N_("Use a GPU backend when the selected runtime provides one."),
             false)

    set_section(N_("Status"), NULL)
    add_string(SUBTITLE_STATUS_DISPLAY, "Model not downloaded",
               N_("Status"),
               N_("Current status of the selected model."), false)
        vlc_config_set(VLC_CONFIG_VOLATILE);

    set_section(N_("Language"), NULL)
    add_string(SUBTITLE_LANGUAGE, "auto",
               N_("Spoken language"),
               N_("Language in the playback audio. Auto detection is slower."),
               false)
        change_string_list(language_values, language_labels)
    add_bool(SUBTITLE_TRANSLATE, false,
             N_("Translate speech to English"),
             N_("Ask Whisper to translate recognized speech to English."),
             false)

    add_savefile(SUBTITLE_OUTPUT, "",
                 N_("Subtitle output file"),
                 N_("SRT file where new subtitle cues will be appended."),
                 false)
        change_private()
    add_loadfile(SUBTITLE_TEXT_SOURCE, "",
                 N_("Subtitle text source"),
                 N_("Optional UTF-8 text file. Each completed cue consumes "
                    "the next non-empty line as subtitle text."),
                 false)
        change_private()
    add_string(SUBTITLE_DEFAULT_TEXT, "Subtitle",
               N_("Fallback subtitle text"),
               N_("Text used when the subtitle text source is empty or unset."),
               false)
        change_private()
    add_integer_with_range(SUBTITLE_QUICK_SECONDS, 4, 1, 120,
                           N_("Quick cue duration"),
                           N_("Number of previous seconds used by the quick "
                              "cue shortcut."),
                           false)
        change_private()
    add_bool(SUBTITLE_AUTORELOAD, true,
             N_("Attach generated subtitle to the current input"),
             N_("Automatically load the generated SRT file into the playing "
                "media after a cue is written."),
             false)
        change_private()

    add_integer(SUBTITLE_KEY_START, KEY_LEFT_BRACKET,
                N_("Set subtitle start key code"),
                N_("VLC key code used to mark current playback time as the "
                   "start of the next subtitle cue."),
                false)
        change_private()
    add_integer(SUBTITLE_KEY_END, KEY_RIGHT_BRACKET,
                N_("Set subtitle end key code"),
                N_("VLC key code used to mark current playback time as the end "
                   "of the cue and append it to the SRT."),
                false)
        change_private()
    add_integer(SUBTITLE_KEY_QUICK, KEY_BACKSLASH,
                N_("Create quick subtitle cue key code"),
                N_("VLC key code used to append a cue covering the previous "
                   "quick-cue duration."),
                false)
        change_private()
    add_integer(SUBTITLE_KEY_RELOAD, KEY_F8,
                N_("Reload generated subtitle key code"),
                N_("VLC key code used to attach the generated SRT file to the "
                   "current input."),
                false)
        change_private()

    set_callbacks(Open, Close)

    add_submodule()
        set_shortname(N_("Subtitle runtime capture"))
        set_description(N_("PCM capture for offline STT runtimes"))
        set_subcategory(SUBCAT_AUDIO_AFILTER)
        set_capability("audio filter", 0)
        add_shortcut(SUBTITLE_FILTER_NAME)
        set_callbacks(OpenRuntimeFilter, CloseRuntimeFilter)
vlc_module_end()

struct subtitle_runtime_result
{
    int64_t start;
    int64_t end;
    const char *text;
};

struct filter_sys_t
{
    subtitle_runtime_t *runtime;
    unsigned channels;
    unsigned sample_rate;
    bool warned_backlog;
};

struct intf_sys_t
{
    vlc_mutex_t lock;
    vlc_mutex_t cue_lock;
    input_thread_t *input;
    vout_thread_t *vout;
    int64_t cue_start;
    unsigned cue_count;
    unsigned text_line;
    bool subtitle_attached;
};

static bool is_empty_string(const char *value)
{
    return value == NULL || value[0] == '\0';
}

static void strip_newline(char *line)
{
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
}

static bool line_has_text(const char *line)
{
    while (*line != '\0')
    {
        if (*line != ' ' && *line != '\t')
            return true;
        line++;
    }
    return false;
}

static char *duplicate_or_empty(const char *value)
{
    char *copy = strdup(value != NULL ? value : "");
    return copy != NULL ? copy : NULL;
}

static char *get_output_path(intf_thread_t *intf)
{
    char *path = var_InheritString(intf, SUBTITLE_OUTPUT);
    if (!is_empty_string(path))
        return path;

    free(path);

    char *documents = config_GetUserDir(VLC_DOCUMENTS_DIR);
    if (documents == NULL)
        return duplicate_or_empty("vlc-subtitle.srt");

    const char *separator =
#ifdef _WIN32
        "\\";
#else
        "/";
#endif
    const char *name = "vlc-subtitle.srt";
    size_t len = strlen(documents) + strlen(separator) + strlen(name) + 1;
    path = malloc(len);
    if (path != NULL)
        snprintf(path, len, "%s%s%s", documents, separator, name);
    free(documents);

    return path;
}

static void format_timestamp(int64_t timestamp, char *buffer, size_t size)
{
    if (timestamp < 0)
        timestamp = 0;

    int64_t milliseconds = timestamp / 1000;
    int64_t hours = milliseconds / 3600000;
    milliseconds %= 3600000;
    int64_t minutes = milliseconds / 60000;
    milliseconds %= 60000;
    int64_t seconds = milliseconds / 1000;
    milliseconds %= 1000;

    snprintf(buffer, size, "%02" PRId64 ":%02" PRId64 ":%02" PRId64
                           ",%03" PRId64,
             hours, minutes, seconds, milliseconds);
}

static void ShowMessage(intf_thread_t *intf, const char *text)
{
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->lock);
    vout_thread_t *vout = sys->vout ? vlc_object_hold(sys->vout) : NULL;
    vlc_mutex_unlock(&sys->lock);

    if (vout != NULL)
    {
        vout_OSDText(vout, VOUT_SPU_CHANNEL_OSD,
                     SUBPICTURE_ALIGN_TOP | SUBPICTURE_ALIGN_LEFT,
                     2 * CLOCK_FREQ, text);
        vlc_object_release(vout);
    }

    msg_Info(intf, "%s", text);
}

static void RuntimeResult(void *opaque, int64_t start, int64_t end,
                          const char *text)
{
    filter_t *filter = opaque;
    const struct subtitle_runtime_result result = { start, end, text };
    var_SetAddress(filter->obj.libvlc, SUBTITLE_RUNTIME_RESULT,
                   (void *)&result);
}

static void SetModelStatus(intf_thread_t *intf, enum model_status status)
{
    const char *state;
    const char *message;

    switch (status)
    {
        case MODEL_STATUS_NOT_DOWNLOADED:
            state = "error";
            message = "Model not downloaded";
            break;
        case MODEL_STATUS_LOADING_CACHE:
            state = "loading";
            message = "Loading from cache...";
            break;
        case MODEL_STATUS_DOWNLOADING_TOKENIZER:
            state = "loading";
            message = "Downloading tokenizer";
            break;
        case MODEL_STATUS_DOWNLOADING_MODEL:
            state = "loading";
            message = "Downloading model";
            break;
        case MODEL_STATUS_INITIALIZING:
            state = "loading";
            message = "Initializing model...";
            break;
        case MODEL_STATUS_READY:
            state = "ready";
            message = "Ready";
            break;
        default:
            vlc_assert_unreachable();
    }

    var_SetString(intf->obj.libvlc, SUBTITLE_RUNTIME_STATE, state);
    var_SetString(intf->obj.libvlc, SUBTITLE_RUNTIME_STATUS, message);
    config_PutPsz(intf, SUBTITLE_STATUS_DISPLAY, message);
    msg_Info(intf, "model status [%s]: %s", state, message);
}

static void RuntimeStatus(void *opaque, const char *state,
                          const char *message)
{
    filter_t *filter = opaque;

    if (strcmp(state, "loading") == 0)
    {
        var_SetInteger(filter->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE,
                       MODEL_STATUS_INITIALIZING);
        return;
    }
    if (strcmp(state, "ready") == 0)
    {
        var_SetInteger(filter->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE,
                       MODEL_STATUS_READY);
        return;
    }
    if (strcmp(state, "error") == 0)
    {
        var_SetInteger(filter->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE,
                       MODEL_STATUS_NOT_DOWNLOADED);
        return;
    }

    var_SetString(filter->obj.libvlc, SUBTITLE_RUNTIME_STATE, state);
    var_SetString(filter->obj.libvlc, SUBTITLE_RUNTIME_STATUS, message);
    config_PutPsz(filter, SUBTITLE_STATUS_DISPLAY, message);
    msg_Info(filter, "STT runtime [%s]: %s", state, message);
}

static block_t *ProcessRuntimeAudio(filter_t *filter, block_t *block)
{
    filter_sys_t *sys = filter->p_sys;
    const int64_t pts = block->i_pts == VLC_TICK_INVALID
                      ? INT64_MIN : block->i_pts;

    if (!subtitle_runtime_push(sys->runtime, (const float *)block->p_buffer,
                               block->i_nb_samples, sys->channels,
                               sys->sample_rate, pts) && !sys->warned_backlog)
    {
        msg_Warn(filter, "STT runtime is not accepting audio; dropping audio");
        sys->warned_backlog = true;
    }
    return block;
}

static void FlushRuntimeAudio(filter_t *filter)
{
    filter_sys_t *sys = filter->p_sys;
    subtitle_runtime_flush(sys->runtime);
    sys->warned_backlog = false;
}

static void CloseRuntimeFilter(vlc_object_t *object)
{
    filter_t *filter = (filter_t *)object;
    filter_sys_t *sys = filter->p_sys;
    subtitle_runtime_destroy(sys->runtime);
    free(sys);
}

static int OpenRuntimeFilter(vlc_object_t *object)
{
    filter_t *filter = (filter_t *)object;
    char *model = var_InheritString(filter, SUBTITLE_MODEL);
    if (is_empty_string(model))
    {
        free(model);
        RuntimeStatus(filter, "error",
                      "Set a local STT model file in preferences");
        return VLC_EGENERIC;
    }

    char *language = var_InheritString(filter, SUBTITLE_LANGUAGE);
    char *backend = var_InheritString(filter, SUBTITLE_BACKEND);
    filter_sys_t *sys = calloc(1, sizeof(*sys));
    if (sys == NULL)
    {
        free(backend);
        free(language);
        free(model);
        return VLC_ENOMEM;
    }

    filter->fmt_in.audio.i_format = VLC_CODEC_FL32;
    filter->fmt_in.i_codec = VLC_CODEC_FL32;
    aout_FormatPrepare(&filter->fmt_in.audio);
    filter->fmt_out.audio = filter->fmt_in.audio;
    filter->fmt_out.i_codec = VLC_CODEC_FL32;

    sys->channels = filter->fmt_in.audio.i_channels;
    sys->sample_rate = filter->fmt_in.audio.i_rate;

    const subtitle_runtime_config_t config = {
        .backend = is_empty_string(backend) ? "whisper" : backend,
        .model_path = model,
        .language = is_empty_string(language) ? "auto" : language,
        .threads = (int)var_InheritInteger(filter, SUBTITLE_THREADS),
        .chunk_ms = (int)var_InheritInteger(filter, SUBTITLE_CHUNK_MS),
        .translate = var_InheritBool(filter, SUBTITLE_TRANSLATE),
        .use_gpu = var_InheritBool(filter, SUBTITLE_USE_GPU),
    };
    sys->runtime = subtitle_runtime_create(&config, RuntimeResult,
                                            RuntimeStatus, filter);
    free(backend);
    free(language);
    free(model);
    if (sys->runtime == NULL)
    {
        free(sys);
        return VLC_EGENERIC;
    }

    filter->p_sys = sys;
    filter->pf_audio_filter = ProcessRuntimeAudio;
    filter->pf_flush = FlushRuntimeAudio;
    return VLC_SUCCESS;
}

static char *read_next_text_line(intf_thread_t *intf)
{
    intf_sys_t *sys = intf->p_sys;
    char *path = var_InheritString(intf, SUBTITLE_TEXT_SOURCE);

    if (is_empty_string(path))
    {
        free(path);
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        msg_Warn(intf, "cannot open subtitle text source '%s': %s", path,
                 vlc_strerror_c(errno));
        free(path);
        return NULL;
    }

    char buffer[4096];
    unsigned current = 0;
    char *result = NULL;

    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        strip_newline(buffer);
        if (!line_has_text(buffer))
            continue;

        if (current++ == sys->text_line)
        {
            result = duplicate_or_empty(buffer);
            if (result != NULL)
                sys->text_line++;
            break;
        }
    }

    fclose(file);
    free(path);
    return result;
}

static char *subtitle_text_for_next_cue(intf_thread_t *intf)
{
    intf_sys_t *sys = intf->p_sys;
    char *line = read_next_text_line(intf);
    if (line != NULL)
        return line;

    char *fallback = var_InheritString(intf, SUBTITLE_DEFAULT_TEXT);
    if (is_empty_string(fallback))
    {
        free(fallback);
        fallback = duplicate_or_empty("Subtitle");
    }

    char numbered[512];
    snprintf(numbered, sizeof(numbered), "%s %u", fallback, sys->cue_count + 1);
    free(fallback);

    return duplicate_or_empty(numbered);
}

static unsigned count_existing_cues(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;

    char buffer[128];
    unsigned highest = 0;

    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        char *end = NULL;
        unsigned long value = strtoul(buffer, &end, 10);
        if (end != buffer && (*end == '\n' || *end == '\r' || *end == '\0') &&
            value > highest)
            highest = (unsigned)value;
    }

    fclose(file);
    return highest;
}

static int append_srt_cue(intf_thread_t *intf, const char *path, int64_t start,
                          int64_t end, const char *text)
{
    intf_sys_t *sys = intf->p_sys;
    vlc_mutex_lock(&sys->cue_lock);
    FILE *file = fopen(path, "ab");

    if (file == NULL)
    {
        msg_Err(intf, "cannot open subtitle output '%s': %s", path,
                vlc_strerror_c(errno));
        vlc_mutex_unlock(&sys->cue_lock);
        return VLC_EGENERIC;
    }

    if (sys->cue_count == 0)
        sys->cue_count = count_existing_cues(path);

    char start_text[32];
    char end_text[32];
    format_timestamp(start, start_text, sizeof(start_text));
    format_timestamp(end, end_text, sizeof(end_text));

    int written = fprintf(file, "%u\n%s --> %s\n%s\n\n", ++sys->cue_count,
                          start_text, end_text, text);
    int close_status = fclose(file);

    if (written < 0 || close_status != 0)
    {
        msg_Err(intf, "cannot write subtitle output '%s': %s", path,
                vlc_strerror_c(errno));
        vlc_mutex_unlock(&sys->cue_lock);
        return VLC_EGENERIC;
    }

    vlc_mutex_unlock(&sys->cue_lock);
    return VLC_SUCCESS;
}

static int AttachSubtitle(intf_thread_t *intf, input_thread_t *input,
                          const char *path)
{
    char *uri = vlc_path2uri(path, NULL);
    if (uri == NULL)
        return VLC_ENOMEM;

    int result = input_AddSlave(input, SLAVE_TYPE_SPU, uri, true, true, false);
    free(uri);

    if (result == VLC_SUCCESS)
    {
        intf->p_sys->subtitle_attached = true;
        ShowMessage(intf, "vlc-subtitle: subtitle file loaded");
    }
    else
        msg_Warn(intf, "could not attach subtitle output to input");

    return result;
}

static int CompleteCue(intf_thread_t *intf, input_thread_t *input, int64_t start,
                       int64_t end)
{
    intf_sys_t *sys = intf->p_sys;

    if (end <= start)
    {
        ShowMessage(intf, "vlc-subtitle: cue end must be after start");
        return VLC_EGENERIC;
    }

    char *path = get_output_path(intf);
    char *text = subtitle_text_for_next_cue(intf);
    if (path == NULL || text == NULL)
    {
        free(path);
        free(text);
        return VLC_ENOMEM;
    }

    int result = append_srt_cue(intf, path, start, end, text);
    if (result == VLC_SUCCESS)
    {
        char message[512];
        snprintf(message, sizeof(message), "vlc-subtitle: saved cue %u",
                 sys->cue_count);
        ShowMessage(intf, message);

        if (var_InheritBool(intf, SUBTITLE_AUTORELOAD) &&
            !sys->subtitle_attached)
            AttachSubtitle(intf, input, path);
    }

    free(path);
    free(text);
    return result;
}

static bool key_matches(intf_thread_t *intf, int64_t pressed,
                        const char *config_name, int64_t fallback)
{
    int64_t configured = var_InheritInteger(intf, config_name);
    return pressed == configured ||
           (configured == KEY_UNSET && pressed == fallback);
}

static int Open(vlc_object_t *this)
{
    intf_thread_t *intf = (intf_thread_t *)this;
    intf_sys_t *sys = calloc(1, sizeof(*sys));

    if (unlikely(sys == NULL))
        return VLC_ENOMEM;

    vlc_mutex_init(&sys->lock);
    vlc_mutex_init(&sys->cue_lock);
    sys->cue_start = -1;
    intf->p_sys = sys;

    var_Create(intf->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE,
               VLC_VAR_INTEGER | VLC_VAR_ISCOMMAND);
    var_AddCallback(intf->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE,
                    ModelStatusEvent, intf);
    var_Create(intf->obj.libvlc, SUBTITLE_RUNTIME_STATE, VLC_VAR_STRING);
    var_Create(intf->obj.libvlc, SUBTITLE_RUNTIME_STATUS, VLC_VAR_STRING);
    var_Create(intf->obj.libvlc, SUBTITLE_RUNTIME_RESULT, VLC_VAR_ADDRESS);
    var_AddCallback(intf->obj.libvlc, SUBTITLE_RUNTIME_RESULT,
                    RuntimeResultEvent, intf);

    var_AddCallback(intf->obj.libvlc, "key-pressed", KeyboardEvent, intf);
    var_AddCallback(pl_Get(intf), "input-current", PlaylistEvent, intf);

    var_SetInteger(intf->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE,
                   MODEL_STATUS_NOT_DOWNLOADED);

    input_thread_t *input = playlist_CurrentInput(pl_Get(intf));
    if (input != NULL)
    {
        ChangeInput(intf, input);
        vlc_object_release(input);
    }

    return VLC_SUCCESS;
}

static void Close(vlc_object_t *this)
{
    intf_thread_t *intf = (intf_thread_t *)this;
    intf_sys_t *sys = intf->p_sys;

    var_DelCallback(pl_Get(intf), "input-current", PlaylistEvent, intf);
    var_DelCallback(intf->obj.libvlc, "key-pressed", KeyboardEvent, intf);

    ChangeInput(intf, NULL);
    var_DelCallback(intf->obj.libvlc, SUBTITLE_RUNTIME_RESULT,
                    RuntimeResultEvent, intf);
    var_Destroy(intf->obj.libvlc, SUBTITLE_RUNTIME_RESULT);
    var_Destroy(intf->obj.libvlc, SUBTITLE_RUNTIME_STATUS);
    var_Destroy(intf->obj.libvlc, SUBTITLE_RUNTIME_STATE);
    var_DelCallback(intf->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE,
                    ModelStatusEvent, intf);
    var_Destroy(intf->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE);
    vlc_mutex_destroy(&sys->cue_lock);
    vlc_mutex_destroy(&sys->lock);
    free(sys);

    msg_Info(intf, "vlc-subtitle module unloaded");
}

static int ModelStatusEvent(vlc_object_t *object, char const *var_name,
                            vlc_value_t old_value, vlc_value_t new_value,
                            void *data)
{
    VLC_UNUSED(object);
    VLC_UNUSED(var_name);
    VLC_UNUSED(old_value);

    if (new_value.i_int >= 0 && new_value.i_int < MODEL_STATUS_COUNT)
        SetModelStatus(data, (enum model_status)new_value.i_int);

    return VLC_SUCCESS;
}

static int RuntimeResultEvent(vlc_object_t *object, char const *var_name,
                              vlc_value_t old_value, vlc_value_t new_value,
                              void *data)
{
    VLC_UNUSED(object);
    VLC_UNUSED(var_name);
    VLC_UNUSED(old_value);

    const struct subtitle_runtime_result *result = new_value.p_address;
    if (result == NULL || is_empty_string(result->text))
        return VLC_SUCCESS;

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;
    vlc_mutex_lock(&sys->lock);
    input_thread_t *input = sys->input ? vlc_object_hold(sys->input) : NULL;
    vlc_mutex_unlock(&sys->lock);

    char *path = get_output_path(intf);
    if (path != NULL &&
        append_srt_cue(intf, path, result->start, result->end,
                       result->text) == VLC_SUCCESS)
    {
        ShowMessage(intf, result->text);
        if (input != NULL && var_InheritBool(intf, SUBTITLE_AUTORELOAD) &&
            !sys->subtitle_attached)
            AttachSubtitle(intf, input, path);
    }

    free(path);
    if (input != NULL)
        vlc_object_release(input);

    return VLC_SUCCESS;
}

static int KeyboardEvent(vlc_object_t *libvlc, char const *var_name,
                         vlc_value_t old_value, vlc_value_t new_value,
                         void *data)
{
    VLC_UNUSED(libvlc);
    VLC_UNUSED(var_name);
    VLC_UNUSED(old_value);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;
    int64_t key = new_value.i_int;

    bool is_start =
        key_matches(intf, key, SUBTITLE_KEY_START, KEY_LEFT_BRACKET);
    bool is_end = key_matches(intf, key, SUBTITLE_KEY_END, KEY_RIGHT_BRACKET);
    bool is_quick = key_matches(intf, key, SUBTITLE_KEY_QUICK, KEY_BACKSLASH);
    bool is_reload = key_matches(intf, key, SUBTITLE_KEY_RELOAD, KEY_F8);

    if (!is_start && !is_end && !is_quick && !is_reload)
        return VLC_SUCCESS;

    vlc_mutex_lock(&sys->lock);
    input_thread_t *input = sys->input ? vlc_object_hold(sys->input) : NULL;
    vlc_mutex_unlock(&sys->lock);

    if (input == NULL)
    {
        ShowMessage(intf, "vlc-subtitle: no active input");
        return VLC_SUCCESS;
    }

    int64_t current = var_GetInteger(input, "time");

    if (is_start)
    {
        sys->cue_start = current;
        ShowMessage(intf, "vlc-subtitle: cue start marked");
    }
    else if (is_end)
    {
        if (sys->cue_start < 0)
            ShowMessage(intf, "vlc-subtitle: mark a start first");
        else
        {
            CompleteCue(intf, input, sys->cue_start, current);
            sys->cue_start = -1;
        }
    }
    else if (is_quick)
    {
        int64_t seconds = var_InheritInteger(intf, SUBTITLE_QUICK_SECONDS);
        int64_t start = current - seconds * CLOCK_FREQ;
        CompleteCue(intf, input, start, current);
    }
    else if (is_reload)
    {
        char *path = get_output_path(intf);
        if (path != NULL)
        {
            AttachSubtitle(intf, input, path);
            free(path);
        }
    }

    vlc_object_release(input);
    return VLC_SUCCESS;
}

static int PlaylistEvent(vlc_object_t *this, char const *var_name,
                         vlc_value_t old_value, vlc_value_t new_value,
                         void *data)
{
    VLC_UNUSED(this);
    VLC_UNUSED(var_name);
    VLC_UNUSED(old_value);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    ChangeInput(intf, new_value.p_address);
    sys->cue_start = -1;
    sys->subtitle_attached = false;

    return VLC_SUCCESS;
}

static void SetAudioFilterEnabled(audio_output_t *aout, const char *name,
                                  bool enabled)
{
    char *current = var_GetString(aout, "audio-filter");
    if (current == NULL)
        current = strdup("");
    if (current == NULL)
        return;

    const size_t capacity = strlen(current) + strlen(name) + 2;
    char *updated = calloc(capacity, 1);
    if (updated == NULL)
    {
        free(current);
        return;
    }

    bool found = false;
    const char *cursor = current;
    while (*cursor != '\0')
    {
        while (*cursor == ' ' || *cursor == ':')
            cursor++;
        const char *end = cursor;
        while (*end != '\0' && *end != ' ' && *end != ':')
            end++;

        const size_t length = (size_t)(end - cursor);
        const bool matches = length == strlen(name) &&
                             strncmp(cursor, name, length) == 0;
        found |= matches;
        if (length > 0 && (enabled || !matches))
        {
            if (updated[0] != '\0')
                strcat(updated, ":");
            strncat(updated, cursor, length);
        }
        cursor = end;
    }

    if (enabled && !found)
    {
        if (updated[0] != '\0')
            strcat(updated, ":");
        strcat(updated, name);
    }

    if (strcmp(current, updated) != 0)
        var_SetString(aout, "audio-filter", updated);
    free(updated);
    free(current);
}

static int InputEvent(vlc_object_t *this, char const *var_name,
                      vlc_value_t old_value, vlc_value_t new_value, void *data)
{
    input_thread_t *input = (input_thread_t *)this;
    intf_thread_t *intf = data;

    VLC_UNUSED(var_name);
    VLC_UNUSED(old_value);

    if (new_value.i_int == INPUT_EVENT_VOUT)
        ChangeVout(intf, input_GetVout(input));
    else if (new_value.i_int == INPUT_EVENT_AOUT)
    {
        audio_output_t *aout = input_GetAout(input);
        if (aout != NULL)
        {
            SetAudioFilterEnabled(aout, SUBTITLE_FILTER_NAME, true);
            vlc_object_release(aout);
        }
    }

    return VLC_SUCCESS;
}

static void SetRuntimeFilter(input_thread_t *input, bool enabled)
{
    if (input == NULL)
        return;

    audio_output_t *aout = input_GetAout(input);
    if (aout != NULL)
    {
        SetAudioFilterEnabled(aout, SUBTITLE_FILTER_NAME, enabled);
        vlc_object_release(aout);
    }
}

static void ChangeInput(intf_thread_t *intf, input_thread_t *input)
{
    intf_sys_t *sys = intf->p_sys;
    input_thread_t *old_input = sys->input;
    vout_thread_t *old_vout = NULL;

    if (old_input != NULL)
    {
        SetRuntimeFilter(old_input, false);
        var_DelCallback(old_input, "intf-event", InputEvent, intf);
        old_vout = sys->vout;
    }

    vlc_mutex_lock(&sys->lock);
    sys->input = input ? vlc_object_hold(input) : NULL;
    sys->vout = NULL;
    vlc_mutex_unlock(&sys->lock);

    if (old_input != NULL)
    {
        if (old_vout != NULL)
            vlc_object_release(old_vout);
        vlc_object_release(old_input);
    }

    if (input != NULL)
    {
        var_AddCallback(input, "intf-event", InputEvent, intf);
        SetRuntimeFilter(input, true);
    }
}

static void ChangeVout(intf_thread_t *intf, vout_thread_t *vout)
{
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->lock);
    vout_thread_t *old_vout = sys->vout;
    sys->vout = vout;
    vlc_mutex_unlock(&sys->lock);

    if (old_vout != NULL)
        vlc_object_release(old_vout);
}
