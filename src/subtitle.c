#include "subtitle.h"
#include "osd.h"
#include "srt.h"
#include "filter.h"

static int Open(vlc_object_t *);
static void Close(vlc_object_t *);

static int EnableEvent(vlc_object_t *, char const *, vlc_value_t,
                       vlc_value_t, void *);
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

static const char *const target_language_values[] = {
    "none", "en", "vi", "zh", "ja", "ko", "es", "fr", "de", "it",
    "pt", "ru", "ar", "hi", "th", "id", "tr", "uk", "nl", "pl",
};

static const char *const target_language_labels[] = {
    N_("None"), N_("English"), N_("Vietnamese"), N_("Chinese"),
    N_("Japanese"), N_("Korean"), N_("Spanish"), N_("French"),
    N_("German"), N_("Italian"), N_("Portuguese"), N_("Russian"),
    N_("Arabic"), N_("Hindi"), N_("Thai"), N_("Indonesian"),
    N_("Turkish"), N_("Ukrainian"), N_("Dutch"), N_("Polish"),
};

static const char *const model_values[] = {
    "tiny.en",
    "tiny",
    "base.en",
    "base",
    "small.en",
    "small",
    "medium.en",
    "medium",
    "large-v1",
    "large-v2",
    "large-v3",
    "large-v3-turbo",
    "voxtral-mini-4b-realtime",
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
    "moonshine-tiny",
    "moonshine-base",
    "moonshine-tiny-streaming",
    "moonshine-base-streaming",
    "moonshine-small-streaming",
    "moonshine-medium-streaming",
};

static const char *const model_labels[] = {
    N_("Whisper tiny.en"),
    N_("Whisper tiny"),
    N_("Whisper base.en"),
    N_("Whisper base"),
    N_("Whisper small.en"),
    N_("Whisper small"),
    N_("Whisper medium.en"),
    N_("Whisper medium"),
    N_("Whisper large-v1"),
    N_("Whisper large-v2"),
    N_("Whisper large-v3"),
    N_("Whisper large-v3-turbo"),
    N_("Voxtral Mini 4B Realtime"),
    N_("Parakeet TDT-CTC 110M"),
    N_("Parakeet CTC 0.6B"),
    N_("Parakeet RNN-T 0.6B"),
    N_("Parakeet TDT 0.6B v2"),
    N_("Parakeet TDT 0.6B v3 Multilingual"),
    N_("Parakeet CTC 1.1B"),
    N_("Parakeet RNN-T 1.1B"),
    N_("Parakeet TDT 1.1B"),
    N_("Parakeet TDT-CTC 1.1B"),
    N_("Parakeet Realtime EOU 120M"),
    N_("Parakeet Nemotron 3.5 ASR Streaming 0.6B"),
    N_("Moonshine Tiny"),
    N_("Moonshine Base"),
    N_("Moonshine Tiny Streaming"),
    N_("Moonshine Base Streaming"),
    N_("Moonshine Small Streaming"),
    N_("Moonshine Medium Streaming"),
};

// clang-format off
vlc_module_begin()
    set_text_domain(DOMAIN)
    set_shortname(N_(SUBTITLE_SHORTNAME))
    set_description(N_("Offline speech-to-text subtitles"))
    set_capability("interface", 100)
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)

    add_bool(SUBTITLE_ENABLE, true,
             N_("Enable"),
             N_("Enable or disable offline speech-to-text subtitle generation."),
             false)

    set_section(N_("Settings · Model"), NULL)
    add_string(SUBTITLE_MODEL_ID, "tiny.en",
               N_("Model"),
               N_("Speech-to-text model to use for live transcription."),
               false)
        change_string_list(model_values, model_labels)
    add_loadfile(SUBTITLE_MODEL_FILE, "",
                 N_("Model file"),
                 N_("Whisper uses a local GGML/GGUF file. Voxtral uses "
                    "consolidated.safetensors from its model directory. "
                    "Parakeet uses a local GGUF file. Moonshine uses a model "
                    "directory; select tokenizer.bin inside it."),
                 false)
    add_integer_with_range(SUBTITLE_THREADS, 4, 1, 32,
                           N_("Inference threads"),
                           N_("CPU threads requested from the selected "
                              "runtime."), false)
    add_integer_with_range(SUBTITLE_CHUNK_MS, 5000, 1000, 30000,
                           N_("Chunk length (ms)"),
                           N_("Audio accumulated before each STT inference. "
                              "Shorter chunks reduce latency but may reduce "
                              "accuracy."), false)
    add_bool(SUBTITLE_USE_GPU, true,
             N_("Use GPU acceleration"),
             N_("Use a GPU backend when the selected runtime provides one."),
             false)

    set_section(N_("Status"), NULL)
    add_string(SUBTITLE_STATUS_DISPLAY, "Not loaded",
               N_("Status"),
               N_("Current status of the selected model."), false)
        vlc_config_set(VLC_CONFIG_VOLATILE);

    set_section(N_("Subtitle Position & Size"), NULL)
    add_integer_with_range(SUBTITLE_POS_X, -1, -1, 4096,
                           N_("X"),
                           N_("Horizontal position in pixels (-1 for default center)."),
                           false)
    add_integer_with_range(SUBTITLE_POS_Y, -1, -1, 4096,
                           N_("Y"),
                           N_("Vertical position in pixels (-1 for default bottom)."),
                           false)
    add_integer_with_range(SUBTITLE_FONT_SIZE, 0, 0, 256,
                           N_("Size"),
                           N_("Subtitle font size in pixels (0 for default size)."),
                           false)

    set_section(N_("Language"), NULL)
    add_string(SUBTITLE_LANGUAGE, "auto",
               N_("Spoken language"),
               N_("Language in the playback audio. Auto detection is slower."),
               false)
        change_string_list(language_values, language_labels)
    add_string(SUBTITLE_TARGET_LANGUAGE, "none",
               N_("Target language"),
               N_("Translate recognized subtitles to the selected language ('None' to disable translation)."),
               false)
        change_string_list(target_language_values, target_language_labels)
    add_bool(SUBTITLE_TRANSLATE, false,
             N_("Translate speech to English"),
             N_("Ask Whisper to translate recognized speech to English."),
             false)
        change_private()

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
        set_description(N_("PCM capture for offline STT engines"))
        set_subcategory(SUBCAT_AUDIO_AFILTER)
        set_capability("audio filter", 100)
        add_shortcut(SUBTITLE_FILTER_NAME)
        set_callbacks(OpenRuntimeFilter, CloseRuntimeFilter)
vlc_module_end()
// clang-format on

static void SetModelStatus(intf_thread_t *intf, enum model_status status)
{
    const char *state;
    const char *message;

    switch (status)
    {
        case MODEL_STATUS_NOT_DOWNLOADED:
            state = "error";
            message = "Not loaded";
            break;
        case MODEL_STATUS_LOADING_CACHE:
            state = "loading";
            message = "Loading...";
            break;
        case MODEL_STATUS_DOWNLOADING_TOKENIZER:
            state = "loading";
            message = "Downloading...";
            break;
        case MODEL_STATUS_DOWNLOADING_MODEL:
            state = "loading";
            message = "Downloading...";
            break;
        case MODEL_STATUS_INITIALIZING:
            state = "loading";
            message = "Initializing...";
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
    sys->spu_channel = VOUT_SPU_CHANNEL_INVALID;
    intf->p_sys = sys;

    var_Create(intf->obj.libvlc, SUBTITLE_ENABLE,
               VLC_VAR_BOOL | VLC_VAR_DOINHERIT);
    var_AddCallback(intf->obj.libvlc, SUBTITLE_ENABLE, EnableEvent, intf);

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

    bool enabled = var_InheritBool(intf, SUBTITLE_ENABLE);
    if (!enabled)
    {
        var_SetString(intf->obj.libvlc, SUBTITLE_RUNTIME_STATE, "disabled");
        var_SetString(intf->obj.libvlc, SUBTITLE_RUNTIME_STATUS, "Disabled");
        config_PutPsz(intf, SUBTITLE_STATUS_DISPLAY, "Disabled");
    }
    else
    {
        var_SetInteger(intf->obj.libvlc, SUBTITLE_MODEL_STATUS_CODE,
                       MODEL_STATUS_NOT_DOWNLOADED);
    }

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
    var_DelCallback(intf->obj.libvlc, SUBTITLE_ENABLE, EnableEvent, intf);
    var_Destroy(intf->obj.libvlc, SUBTITLE_ENABLE);
    vlc_mutex_destroy(&sys->cue_lock);
    vlc_mutex_destroy(&sys->lock);
    free(sys);

    msg_Info(intf, "vlc-subtitle module unloaded");
}

static int EnableEvent(vlc_object_t *object, char const *var_name,
                       vlc_value_t old_value, vlc_value_t new_value,
                       void *data)
{
    VLC_UNUSED(object);
    VLC_UNUSED(var_name);
    VLC_UNUSED(old_value);
    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->lock);
    input_thread_t *input = sys->input ? vlc_object_hold(sys->input) : NULL;
    vlc_mutex_unlock(&sys->lock);

    if (input != NULL)
    {
        SetRuntimeFilter(input, new_value.b_bool);
        vlc_object_release(input);
    }

    if (new_value.b_bool)
    {
        SetModelStatus(intf, MODEL_STATUS_NOT_DOWNLOADED);
        msg_Info(intf, "vlc-subtitle: plugin enabled");
    }
    else
    {
        var_SetString(intf->obj.libvlc, SUBTITLE_RUNTIME_STATE, "disabled");
        var_SetString(intf->obj.libvlc, SUBTITLE_RUNTIME_STATUS, "Disabled");
        config_PutPsz(intf, SUBTITLE_STATUS_DISPLAY, "Disabled");
        msg_Info(intf, "vlc-subtitle: plugin disabled");
    }

    return VLC_SUCCESS;
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

    intf_thread_t *intf = data;
    if (!var_InheritBool(intf, SUBTITLE_ENABLE))
        return VLC_SUCCESS;

    const struct subtitle_runtime_result *result = new_value.p_address;
    if (result == NULL || is_empty_string(result->text))
        return VLC_SUCCESS;

    intf_sys_t *sys = intf->p_sys;
    vlc_mutex_lock(&sys->lock);
    input_thread_t *input = sys->input ? vlc_object_hold(sys->input) : NULL;
    vlc_mutex_unlock(&sys->lock);

    int64_t start = result->start;
    int64_t end = result->end;

    if (input != NULL)
    {
        int64_t current = var_GetInteger(input, "time");
        if (current > 0)
        {
            int64_t duration = (end > start) ? (end - start) : (2 * CLOCK_FREQ);
            end = current;
            start = (current > duration) ? (current - duration) : 0;
        }
    }

    char *path = get_output_path(intf);
    if (path != NULL &&
        append_srt_cue(intf, path, start, end,
                       result->text) == VLC_SUCCESS)
    {
        ShowMessage(intf, result->text);
        if (input != NULL && var_InheritBool(intf, SUBTITLE_AUTORELOAD) &&
            !sys->subtitle_attached)
        {
            AttachSubtitle(intf, input, path);
        }
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
    if (!var_InheritBool(intf, SUBTITLE_ENABLE))
        return VLC_SUCCESS;

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
        {
            ShowMessage(intf, "vlc-subtitle: mark a start first");
        }
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

static int InputEvent(vlc_object_t *this, char const *var_name,
                      vlc_value_t old_value, vlc_value_t new_value, void *data)
{
    input_thread_t *input = (input_thread_t *)this;
    intf_thread_t *intf = data;

    VLC_UNUSED(var_name);
    VLC_UNUSED(old_value);

    if (new_value.i_int == INPUT_EVENT_VOUT)
    {
        ChangeVout(intf, input_GetVout(input));
    }
    else if (new_value.i_int == INPUT_EVENT_AOUT)
    {
        audio_output_t *aout = input_GetAout(input);
        if (aout != NULL)
        {
            bool enabled = var_InheritBool(intf, SUBTITLE_ENABLE);
            SetAudioFilterEnabled(aout, SUBTITLE_FILTER_NAME, enabled);
            vlc_object_release(aout);
        }
    }

    return VLC_SUCCESS;
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
    sys->spu_channel = VOUT_SPU_CHANNEL_INVALID;
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
        bool enabled = var_InheritBool(intf, SUBTITLE_ENABLE);
        SetRuntimeFilter(input, enabled);
    }
}

static void ChangeVout(intf_thread_t *intf, vout_thread_t *vout)
{
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->lock);
    vout_thread_t *old_vout = sys->vout;
    sys->vout = vout;
    sys->spu_channel = VOUT_SPU_CHANNEL_INVALID;
    vlc_mutex_unlock(&sys->lock);

    if (old_vout != NULL)
        vlc_object_release(old_vout);
}
