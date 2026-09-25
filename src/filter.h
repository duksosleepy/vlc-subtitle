#pragma once

#include "subtitle.h"

int OpenRuntimeFilter(vlc_object_t *object);
void CloseRuntimeFilter(vlc_object_t *object);
void SetAudioFilterEnabled(audio_output_t *aout, const char *name, bool enabled);
void SetRuntimeFilter(input_thread_t *input, bool enabled);
