#pragma once

#include "subtitle.h"

void DisplaySubtitleText(intf_thread_t *intf, vout_thread_t *vout, int channel, const char *text,
                         vlc_tick_t duration);
void ShowMessage(intf_thread_t *intf, const char *text);
