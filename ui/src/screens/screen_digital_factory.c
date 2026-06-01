/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Digital Factory status controls.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *df_screen = NULL;
static lv_obj_t *info_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_timer_t *status_timer = NULL;
static int disconnect_armed = 0;

#define DF_LOCK_DIR "/tmp/deneb-df-lock"

static void read_command(char *dst, size_t dst_size, const char *cmd)
{
    if (dst_size == 0)
        return;
    dst[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return;
    fgets(dst, dst_size, fp);
    pclose(fp);
    size_t len = strlen(dst);
    while (len > 0 && (dst[len - 1] == '\n' || dst[len - 1] == '\r'))
        dst[--len] = '\0';
}

static void restart_cb(lv_event_t *e)
{
    (void)e;
    system("/etc/init.d/digitalfactory restart >/tmp/deneb-df.log 2>&1 &");
    lv_label_set_text(status_label, locale_get("digital_factory.restarting"));
}

static int bridge_is_busy(void)
{
    return system("[ -d " DF_LOCK_DIR " ]") == 0;
}

static void refresh_status(void)
{
    char status[160];
    char cluster[96];
    char state[96];
    char text[320];

    read_command(cluster, sizeof(cluster),
                 "uci -q get ultimaker.option.cluster_id 2>/dev/null || echo none");
    read_command(state, sizeof(state),
                 "/etc/init.d/digitalfactory enabled >/dev/null 2>&1 && echo enabled || echo disabled");
    snprintf(text, sizeof(text), "%s\n%s: %s\n%s: %s",
             locale_get("digital_factory.title"),
             locale_get("digital_factory.cluster"), cluster,
             locale_get("digital_factory.service"), state);
    if (info_label)
        lv_label_set_text(info_label, text);

    read_command(status, sizeof(status),
                 "cat /tmp/deneb-df-status 2>/dev/null || true");
    if (status[0] && status_label)
        lv_label_set_text(status_label, status);
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_status();
}

static void connect_cb(lv_event_t *e)
{
    (void)e;
    if (bridge_is_busy()) {
        lv_label_set_text(status_label,
                          locale_get("digital_factory.request_running"));
        return;
    }

    system("rm -f /tmp/deneb-df-status; "
           "(mkdir " DF_LOCK_DIR " 2>/dev/null || exit 0; "
           "/usr/bin/deneb-df-bridge connect --timeout 35 "
           ">/tmp/deneb-df-status 2>&1; "
           "rmdir " DF_LOCK_DIR " 2>/dev/null) &");
    lv_label_set_text(status_label, locale_get("digital_factory.requesting_pin"));
}

static void disconnect_cb(lv_event_t *e)
{
    (void)e;
    if (!disconnect_armed) {
        disconnect_armed = 1;
        lv_label_set_text(status_label,
                          locale_get("digital_factory.tap_disconnect"));
        return;
    }

    if (bridge_is_busy()) {
        lv_label_set_text(status_label,
                          locale_get("digital_factory.request_running"));
        return;
    }

    disconnect_armed = 0;
    system("rm -f /tmp/deneb-df-status; "
           "(mkdir " DF_LOCK_DIR " 2>/dev/null || exit 0; "
           "/usr/bin/deneb-df-bridge disconnect --timeout 10 "
           ">/tmp/deneb-df-status 2>&1 || "
           "(uci -q delete ultimaker.option.cluster_id; "
           "uci -q commit ultimaker; "
           "/etc/init.d/digitalfactory restart); "
           "rmdir " DF_LOCK_DIR " 2>/dev/null) >/tmp/deneb-df.log 2>&1 &");
    lv_label_set_text(status_label,
                      locale_get("digital_factory.disconnect_requested"));
}

static lv_obj_t *df_create(void)
{
    char cluster[96];
    char state[96];
    char text[320];

    read_command(cluster, sizeof(cluster),
                 "uci -q get ultimaker.option.cluster_id 2>/dev/null || echo none");
    read_command(state, sizeof(state),
                 "/etc/init.d/digitalfactory enabled >/dev/null 2>&1 && echo enabled || echo disabled");
    snprintf(text, sizeof(text), "%s\n%s: %s\n%s: %s",
             locale_get("digital_factory.title"),
             locale_get("digital_factory.cluster"), cluster,
             locale_get("digital_factory.service"), state);

    df_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(df_screen, 320, 208);
    lv_obj_align(df_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(df_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(df_screen, 0, 0);
    lv_obj_set_style_border_width(df_screen, 0, 0);
    lv_obj_set_style_pad_all(df_screen, 12, 0);
    lv_obj_set_flex_flow(df_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(df_screen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(df_screen, 12, 0);
    lv_obj_set_scroll_dir(df_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(df_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(df_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(df_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    info_label = lv_label_create(df_screen);
    lv_label_set_text(info_label, text);
    lv_obj_set_width(info_label, 280);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xe0e0e0), 0);

    lv_obj_t *connect_btn = lv_button_create(df_screen);
    lv_obj_set_size(connect_btn, 240, 36);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(connect_btn, 4, 0);
    lv_obj_add_event_cb(connect_btn, connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, locale_get("digital_factory.pair_show_pin"));
    lv_obj_center(connect_lbl);

    lv_obj_t *btn = lv_button_create(df_screen);
    lv_obj_set_size(btn, 240, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, restart_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, locale_get("digital_factory.restart"));
    lv_obj_center(lbl);

    lv_obj_t *disconnect_btn = lv_button_create(df_screen);
    lv_obj_set_size(disconnect_btn, 240, 36);
    lv_obj_set_style_bg_color(disconnect_btn, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(disconnect_btn, 4, 0);
    lv_obj_add_event_cb(disconnect_btn, disconnect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *disconnect_lbl = lv_label_create(disconnect_btn);
    lv_label_set_text(disconnect_lbl, locale_get("digital_factory.disconnect"));
    lv_obj_center(disconnect_lbl);

    status_label = lv_label_create(df_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_width(status_label, 280);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);

    status_timer = lv_timer_create(status_timer_cb, 1000, NULL);

    return df_screen;
}

static void df_destroy(void)
{
    if (status_timer) {
        lv_timer_delete(status_timer);
        status_timer = NULL;
    }
    df_screen = NULL;
    info_label = NULL;
    status_label = NULL;
    disconnect_armed = 0;
}

const screen_ops_t screen_digital_factory = {
    .name = "settings.digital_factory",
    .create = df_create,
    .destroy = df_destroy,
    .show_back = true,
};
