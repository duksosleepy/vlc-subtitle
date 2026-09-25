#include "osd.h"

typedef struct subpicture_updater_sys_t {
    int32_t pos_x;
    int32_t pos_y;
    int32_t font_size;
    char *text;
} subpicture_updater_sys_t;

static int SubOSDValidate(subpicture_t *subpic, bool has_src_changed, const video_format_t *fmt_src,
                          bool has_dst_changed, const video_format_t *fmt_dst, vlc_tick_t ts) {
    VLC_UNUSED(subpic);
    VLC_UNUSED(ts);
    VLC_UNUSED(fmt_src);
    VLC_UNUSED(has_src_changed);
    VLC_UNUSED(fmt_dst);

    if (!has_dst_changed)
        return VLC_SUCCESS;
    return VLC_EGENERIC;
}

static void SubOSDUpdate(subpicture_t *subpic, const video_format_t *fmt_src,
                         const video_format_t *fmt_dst, vlc_tick_t ts) {
    subpicture_updater_sys_t *sys = subpic->updater.p_sys;
    VLC_UNUSED(fmt_src);
    VLC_UNUSED(ts);

    if (fmt_dst->i_sar_num <= 0 || fmt_dst->i_sar_den <= 0)
        return;

    subpic->i_original_picture_width =
        fmt_dst->i_visible_width * fmt_dst->i_sar_num / fmt_dst->i_sar_den;
    subpic->i_original_picture_height = fmt_dst->i_visible_height;

    video_format_t fmt;
    video_format_Init(&fmt, VLC_CODEC_TEXT);
    fmt.i_sar_num = 1;
    fmt.i_sar_den = 1;

    subpicture_region_t *r = subpicture_region_New(&fmt);
    if (!r)
        return;
    subpic->p_region = r;

    r->p_text = text_segment_New(sys->text);
    if (!r->p_text)
        return;

    if (sys->font_size > 0) {
        r->p_text->style = text_style_New();
        if (r->p_text->style)
            r->p_text->style->i_font_size = sys->font_size;
    }

    const float margin_ratio = 0.04f;
    const int margin_v = (int)(margin_ratio * fmt_dst->i_visible_height);

    if (sys->pos_x >= 0 && sys->pos_y >= 0) {
        subpic->b_absolute = true;
        r->i_align = SUBPICTURE_ALIGN_LEFT | SUBPICTURE_ALIGN_TOP;
        r->i_text_align = SUBPICTURE_ALIGN_LEFT;
        r->i_x = sys->pos_x;
        r->i_y = sys->pos_y;
    } else if (sys->pos_x >= 0) {
        subpic->b_absolute = true;
        r->i_align = SUBPICTURE_ALIGN_LEFT | SUBPICTURE_ALIGN_BOTTOM;
        r->i_text_align = SUBPICTURE_ALIGN_LEFT;
        r->i_x = sys->pos_x;
        r->i_y = margin_v - fmt_dst->i_y_offset;
    } else if (sys->pos_y >= 0) {
        subpic->b_absolute = false;
        r->i_align = SUBPICTURE_ALIGN_TOP;
        r->i_text_align = 0;
        r->i_x = 0;
        r->i_y = sys->pos_y;
    } else {
        subpic->b_absolute = false;
        r->i_align = SUBPICTURE_ALIGN_BOTTOM;
        r->i_text_align = SUBPICTURE_ALIGN_BOTTOM;
        r->i_x = 0;
        r->i_y = margin_v - fmt_dst->i_y_offset;
    }
}

static void SubOSDDestroy(subpicture_t *subpic) {
    subpicture_updater_sys_t *sys = subpic->updater.p_sys;
    if (sys) {
        free(sys->text);
        free(sys);
    }
}

void DisplaySubtitleText(intf_thread_t *intf, vout_thread_t *vout, int channel, const char *text,
                         vlc_tick_t duration) {
    int32_t pos_x = (int32_t)var_InheritInteger(intf, SUBTITLE_POS_X);
    int32_t pos_y = (int32_t)var_InheritInteger(intf, SUBTITLE_POS_Y);
    int32_t font_size = (int32_t)var_InheritInteger(intf, SUBTITLE_FONT_SIZE);

    if (pos_x < 0 && pos_y < 0 && font_size <= 0) {
        vout_OSDText(vout, channel, SUBPICTURE_ALIGN_BOTTOM, duration, text);
        return;
    }

    subpicture_updater_sys_t *updater_sys = calloc(1, sizeof(*updater_sys));
    if (!updater_sys)
        return;
    updater_sys->pos_x = pos_x;
    updater_sys->pos_y = pos_y;
    updater_sys->font_size = font_size;
    updater_sys->text = strdup(text);
    if (!updater_sys->text) {
        free(updater_sys);
        return;
    }

    subpicture_updater_t updater = {
        .pf_validate = SubOSDValidate,
        .pf_update = SubOSDUpdate,
        .pf_destroy = SubOSDDestroy,
        .p_sys = updater_sys,
    };

    subpicture_t *subpic = subpicture_New(&updater);
    if (!subpic) {
        free(updater_sys->text);
        free(updater_sys);
        return;
    }

    subpic->i_channel = channel;
    subpic->i_start = mdate();
    subpic->i_stop = subpic->i_start + duration;
    subpic->b_ephemer = true;
    subpic->b_absolute = (pos_x >= 0 && pos_y >= 0);
    subpic->b_fade = true;

    vout_PutSubpicture(vout, subpic);
}

void ShowMessage(intf_thread_t *intf, const char *text) {
    intf_sys_t *sys = intf->p_sys;

    vlc_mutex_lock(&sys->lock);
    vout_thread_t *vout = sys->vout ? vlc_object_hold(sys->vout) : NULL;
    input_thread_t *input = sys->input ? vlc_object_hold(sys->input) : NULL;
    vlc_mutex_unlock(&sys->lock);

    if (input == NULL)
        input = playlist_CurrentInput(pl_Get(intf));

    vout_thread_t *fresh_vout = NULL;
    if (input != NULL)
        fresh_vout = input_GetVout(input);

    vout_thread_t *target_vout = fresh_vout != NULL ? fresh_vout : vout;
    if (target_vout != NULL) {
        vlc_mutex_lock(&sys->lock);
        if (sys->spu_channel <= 0)
            sys->spu_channel = vout_RegisterSubpictureChannel(target_vout);
        int chan = sys->spu_channel > 0 ? sys->spu_channel : VOUT_SPU_CHANNEL_OSD;
        vlc_mutex_unlock(&sys->lock);

        vout_FlushSubpictureChannel(target_vout, chan);
        DisplaySubtitleText(intf, target_vout, chan, text, 4 * CLOCK_FREQ);
    } else {
        msg_Dbg(intf, "no active video output found to display text: %s", text);
    }

    if (fresh_vout != NULL)
        vlc_object_release(fresh_vout);
    if (vout != NULL)
        vlc_object_release(vout);
    if (input != NULL)
        vlc_object_release(input);

    msg_Info(intf, "%s", text);
}
