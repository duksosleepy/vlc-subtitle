#include "srt.h"
#include "osd.h"

static void strip_newline(char *line) {
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
}

static bool line_has_text(const char *line) {
    while (*line != '\0') {
        if (*line != ' ' && *line != '\t')
            return true;
        line++;
    }
    return false;
}

char *get_output_path(intf_thread_t *intf) {
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
    path = calloc(len, sizeof(*path));
    if (path != NULL)
        snprintf(path, len, "%s%s%s", documents, separator, name);
    free(documents);

    return path;
}

void format_timestamp(int64_t timestamp, char *buffer, size_t size) {
    if (timestamp < 0)
        timestamp = 0;

    int64_t milliseconds = timestamp / 1000;
    int64_t hours = milliseconds / 3600000;
    milliseconds %= 3600000;
    int64_t minutes = milliseconds / 60000;
    milliseconds %= 60000;
    int64_t seconds = milliseconds / 1000;
    milliseconds %= 1000;

    snprintf(buffer, size, "%02" PRId64 ":%02" PRId64 ":%02" PRId64 ",%03" PRId64, hours, minutes,
             seconds, milliseconds);
}

static char *read_next_text_line(intf_thread_t *intf) {
    intf_sys_t *sys = intf->p_sys;
    char *path = var_InheritString(intf, SUBTITLE_TEXT_SOURCE);

    if (is_empty_string(path)) {
        free(path);
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        msg_Warn(intf, "cannot open subtitle text source '%s': %s", path, vlc_strerror_c(errno));
        free(path);
        return NULL;
    }

    char buffer[4096];
    uint32_t current = 0;
    char *result = NULL;

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        strip_newline(buffer);
        if (!line_has_text(buffer))
            continue;

        if (current++ == sys->text_line) {
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

static char *subtitle_text_for_next_cue(intf_thread_t *intf) {
    intf_sys_t *sys = intf->p_sys;
    char *line = read_next_text_line(intf);
    if (line != NULL)
        return line;

    char *fallback = var_InheritString(intf, SUBTITLE_DEFAULT_TEXT);
    if (is_empty_string(fallback)) {
        free(fallback);
        fallback = duplicate_or_empty("Subtitle");
    }

    char numbered[512];
    snprintf(numbered, sizeof(numbered), "%s %" PRIu32, fallback, sys->cue_count + 1);
    free(fallback);

    return duplicate_or_empty(numbered);
}

static uint32_t count_existing_cues(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;

    char buffer[128];
    uint32_t highest = 0;

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        char *end = NULL;
        unsigned long value = strtoul(buffer, &end, 10);
        if (end != buffer && (*end == '\n' || *end == '\r' || *end == '\0') && value > highest)
            highest = (uint32_t)value;
    }

    fclose(file);
    return highest;
}

int append_srt_cue(intf_thread_t *intf, const char *path, int64_t start, int64_t end,
                   const char *text) {
    intf_sys_t *sys = intf->p_sys;
    vlc_mutex_lock(&sys->cue_lock);
    FILE *file = fopen(path, "ab");

    if (file == NULL) {
        msg_Err(intf, "cannot open subtitle output '%s': %s", path, vlc_strerror_c(errno));
        vlc_mutex_unlock(&sys->cue_lock);
        return VLC_EGENERIC;
    }

    if (sys->cue_count == 0)
        sys->cue_count = count_existing_cues(path);

    char start_text[32];
    char end_text[32];
    format_timestamp(start, start_text, sizeof(start_text));
    format_timestamp(end, end_text, sizeof(end_text));

    int written = fprintf(file, "%" PRIu32 "\n%s --> %s\n%s\n\n", ++sys->cue_count, start_text,
                          end_text, text);
    int close_status = fclose(file);

    if (written < 0 || close_status != 0) {
        msg_Err(intf, "cannot write subtitle output '%s': %s", path, vlc_strerror_c(errno));
        vlc_mutex_unlock(&sys->cue_lock);
        return VLC_EGENERIC;
    }

    vlc_mutex_unlock(&sys->cue_lock);
    return VLC_SUCCESS;
}

int AttachSubtitle(intf_thread_t *intf, input_thread_t *input, const char *path) {
    char *uri = vlc_path2uri(path, NULL);
    if (uri == NULL)
        return VLC_ENOMEM;

    int result = input_AddSlave(input, SLAVE_TYPE_SPU, uri, false, false, false);
    free(uri);

    if (result == VLC_SUCCESS) {
        intf->p_sys->subtitle_attached = true;
        msg_Info(intf, "vlc-subtitle: subtitle file loaded");
    } else {
        msg_Warn(intf, "could not attach subtitle output to input");
    }

    return result;
}

int CompleteCue(intf_thread_t *intf, input_thread_t *input, int64_t start, int64_t end) {
    intf_sys_t *sys = intf->p_sys;

    if (end <= start) {
        ShowMessage(intf, "vlc-subtitle: cue end must be after start");
        return VLC_EGENERIC;
    }

    char *path = get_output_path(intf);
    char *text = subtitle_text_for_next_cue(intf);
    if (path == NULL || text == NULL) {
        free(path);
        free(text);
        return VLC_ENOMEM;
    }

    int result = append_srt_cue(intf, path, start, end, text);
    if (result == VLC_SUCCESS) {
        char message[512];
        snprintf(message, sizeof(message), "vlc-subtitle: saved cue %" PRIu32, sys->cue_count);
        ShowMessage(intf, message);

        if (var_InheritBool(intf, SUBTITLE_AUTORELOAD) && !sys->subtitle_attached) {
            AttachSubtitle(intf, input, path);
        }
    }

    free(path);
    free(text);
    return result;
}
