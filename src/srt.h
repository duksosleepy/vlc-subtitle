#pragma once

#include "subtitle.h"

char *get_output_path(intf_thread_t *intf);
void format_timestamp(int64_t timestamp, char *buffer, size_t size);
int append_srt_cue(intf_thread_t *intf, const char *path, int64_t start, int64_t end,
                   const char *text);
int AttachSubtitle(intf_thread_t *intf, input_thread_t *input, const char *path);
int CompleteCue(intf_thread_t *intf, input_thread_t *input, int64_t start, int64_t end);
