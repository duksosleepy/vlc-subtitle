/*****************************************************************************
 * subtitle.c: VLC subtitle offline control module
 *****************************************************************************
 * Copyright (C) 2026 vlc-subtitle contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *****************************************************************************/

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
#include <vlc_configuration.h>
#include <vlc_input.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_plugin.h>
#include <vlc_url.h>

#define SUBTITLE_OUTPUT "subtitle-output"
#define SUBTITLE_TEXT_SOURCE "subtitle-text-source"
#define SUBTITLE_DEFAULT_TEXT "subtitle-default-text"
#define SUBTITLE_QUICK_SECONDS "subtitle-quick-seconds"
#define SUBTITLE_AUTORELOAD "subtitle-autoreload"

#define SUBTITLE_KEY_START "subtitle-key-start"
#define SUBTITLE_KEY_END "subtitle-key-end"
#define SUBTITLE_KEY_QUICK "subtitle-key-quick"
#define SUBTITLE_KEY_RELOAD "subtitle-key-reload"

#define KEY_LEFT_BRACKET 0x0000005b
#define KEY_RIGHT_BRACKET 0x0000005d
#define KEY_BACKSLASH 0x0000005c

static int Open(vlc_object_t *);
static void Close(vlc_object_t *);

static int KeyboardEvent(vlc_object_t *, char const *, vlc_value_t,
                         vlc_value_t, void *);
static int PlaylistEvent(vlc_object_t *, char const *, vlc_value_t,
                         vlc_value_t, void *);
static int InputEvent(vlc_object_t *, char const *, vlc_value_t,
                      vlc_value_t, void *);

static void ChangeInput(intf_thread_t *, input_thread_t *);
static void ChangeVout(intf_thread_t *, vout_thread_t *);

vlc_module_begin()
    set_text_domain(DOMAIN)
    set_shortname(N_("Subtitle Offline"))
    set_description(N_("Create SRT subtitles while playing media"))
    set_help(N_("<sup><b>vlc-subtitle</b></sup><br/>"
                "Use [ to mark subtitle start, ] to mark subtitle end and "
                "append a cue, \\ to create a quick cue for the previous "
                "few seconds, and F8 to reload the generated subtitle file."))
    set_capability("interface", 0)
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)

    add_savefile(SUBTITLE_OUTPUT, "",
                 N_("Subtitle output file"),
                 N_("SRT file where new subtitle cues will be appended."),
                 false)
    add_loadfile(SUBTITLE_TEXT_SOURCE, "",
                 N_("Subtitle text source"),
                 N_("Optional UTF-8 text file. Each completed cue consumes "
                    "the next non-empty line as subtitle text."),
                 false)
    add_string(SUBTITLE_DEFAULT_TEXT, "Subtitle",
               N_("Fallback subtitle text"),
               N_("Text used when the subtitle text source is empty or unset."),
               false)
    add_integer_with_range(SUBTITLE_QUICK_SECONDS, 4, 1, 120,
                           N_("Quick cue duration"),
                           N_("Number of previous seconds used by the quick "
                              "cue shortcut."),
                           false)
    add_bool(SUBTITLE_AUTORELOAD, true,
             N_("Attach generated subtitle to the current input"),
             N_("Automatically load the generated SRT file into the playing "
                "media after a cue is written."),
             false)

    add_integer(SUBTITLE_KEY_START, KEY_LEFT_BRACKET,
                N_("Set subtitle start key code"),
                N_("VLC key code used to mark current playback time as the "
                   "start of the next subtitle cue."),
                false)
    add_integer(SUBTITLE_KEY_END, KEY_RIGHT_BRACKET,
                N_("Set subtitle end key code"),
                N_("VLC key code used to mark current playback time as the end "
                   "of the cue and append it to the SRT."),
                false)
    add_integer(SUBTITLE_KEY_QUICK, KEY_BACKSLASH,
                N_("Create quick subtitle cue key code"),
                N_("VLC key code used to append a cue covering the previous "
                   "quick-cue duration."),
                false)
    add_integer(SUBTITLE_KEY_RELOAD, KEY_F8,
                N_("Reload generated subtitle key code"),
                N_("VLC key code used to attach the generated SRT file to the "
                   "current input."),
                false)

    set_callbacks(Open, Close)
vlc_module_end()

struct intf_sys_t
{
    vlc_mutex_t lock;
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
    FILE *file = fopen(path, "ab");

    if (file == NULL)
    {
        msg_Err(intf, "cannot open subtitle output '%s': %s", path,
                vlc_strerror_c(errno));
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
        return VLC_EGENERIC;
    }

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
    sys->cue_start = -1;
    intf->p_sys = sys;

    var_AddCallback(intf->obj.libvlc, "key-pressed", KeyboardEvent, intf);
    var_AddCallback(pl_Get(intf), "input-current", PlaylistEvent, intf);

    msg_Info(intf, "vlc-subtitle module loaded");
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *this)
{
    intf_thread_t *intf = (intf_thread_t *)this;
    intf_sys_t *sys = intf->p_sys;

    var_DelCallback(pl_Get(intf), "input-current", PlaylistEvent, intf);
    var_DelCallback(intf->obj.libvlc, "key-pressed", KeyboardEvent, intf);

    ChangeInput(intf, NULL);
    vlc_mutex_destroy(&sys->lock);
    free(sys);

    msg_Info(intf, "vlc-subtitle module unloaded");
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

static int InputEvent(vlc_object_t *this, char const *var_name,
                      vlc_value_t old_value, vlc_value_t new_value, void *data)
{
    input_thread_t *input = (input_thread_t *)this;
    intf_thread_t *intf = data;

    VLC_UNUSED(var_name);
    VLC_UNUSED(old_value);

    if (new_value.i_int == INPUT_EVENT_VOUT)
        ChangeVout(intf, input_GetVout(input));

    return VLC_SUCCESS;
}

static void ChangeInput(intf_thread_t *intf, input_thread_t *input)
{
    intf_sys_t *sys = intf->p_sys;
    input_thread_t *old_input = sys->input;
    vout_thread_t *old_vout = NULL;

    if (old_input != NULL)
    {
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
        var_AddCallback(input, "intf-event", InputEvent, intf);
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
