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
static lv_obj_t *connect_button = NULL;
static lv_obj_t *disconnect_button = NULL;
static lv_timer_t *status_timer = NULL;
static int disconnect_armed = 0;

#define DF_LOCK_DIR "/tmp/deneb-df-lock"

static int status_value(const char *status, const char *key,
                        char *out, size_t out_size);

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

static int bridge_is_busy(void)
{
    return system("[ -d " DF_LOCK_DIR " ]") == 0;
}

static int has_cloud_binding(void)
{
    return system("uci -q get ultimaker.option.cluster_id >/dev/null 2>&1") == 0;
}

static int is_active_df_state(const char *status)
{
    char state[32];

    if (!status_value(status, "state", state, sizeof(state)))
        return 0;
    return strcmp(state, "connected") == 0 ||
           strcmp(state, "reconnecting") == 0 ||
           strcmp(state, "connecting") == 0 ||
           strcmp(state, "enter_pin") == 0 ||
           strcmp(state, "disconnecting") == 0;
}

static void update_button_states(const char *status)
{
    int disconnecting = status && strstr(status, "state=disconnecting");
    int can_disconnect = !disconnecting &&
                         (has_cloud_binding() || is_active_df_state(status));

    if (!connect_button)
        goto disconnect;
    if (status && strstr(status, "state=connected"))
        lv_obj_add_state(connect_button, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(connect_button, LV_STATE_DISABLED);

disconnect:
    if (!disconnect_button)
        return;
    if (can_disconnect) {
        lv_obj_remove_state(disconnect_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(disconnect_button, lv_color_hex(0xe94560), 0);
    } else {
        disconnect_armed = 0;
        lv_obj_add_state(disconnect_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(disconnect_button, lv_color_hex(0x555b66), 0);
    }
}

static int status_value(const char *status, const char *key,
                        char *out, size_t out_size)
{
    char needle[32];
    const char *start;
    size_t len;

    if (!status || !key || !out || out_size == 0)
        return 0;
    out[0] = '\0';
    snprintf(needle, sizeof(needle), "%s=", key);
    start = strstr(status, needle);
    if (!start)
        return 0;
    start += strlen(needle);
    len = strcspn(start, "\r\n ");
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return out[0] != '\0';
}

static void format_status_text(const char *status, char *out, size_t out_size)
{
    char state[32];
    char pin[32];

    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!status_value(status, "state", state, sizeof(state))) {
        char reason[64];
        if (status_value(status, "reason", reason, sizeof(reason)) &&
            strcmp(reason, "no-cloud-token") == 0) {
            snprintf(out, out_size, "%s",
                     locale_get("digital_factory.status_cloud_signin_required"));
        } else if (status && strstr(status, "status=error")) {
            snprintf(out, out_size, "%s",
                     locale_get("digital_factory.status_disconnect_failed"));
        } else {
            snprintf(out, out_size, "%s", status ? status : "");
        }
        return;
    }

    if (strcmp(state, "connected") == 0) {
        snprintf(out, out_size, "%s",
                 locale_get("digital_factory.status_connected"));
    } else if (strcmp(state, "disconnected") == 0) {
        snprintf(out, out_size, "%s",
                 locale_get("digital_factory.status_disconnected"));
    } else if (strcmp(state, "reconnecting") == 0 ||
               strcmp(state, "connecting") == 0 ||
               strcmp(state, "disconnecting") == 0) {
        snprintf(out, out_size, "%s",
                 locale_get("digital_factory.status_reconnecting"));
    } else if (strcmp(state, "enter_pin") == 0 &&
               status_value(status, "pin", pin, sizeof(pin))) {
        locale_format_s(out, out_size, "digital_factory.status_pin_fmt", pin);
    } else {
        snprintf(out, out_size, "%s", status);
    }
}

static lv_color_t status_text_color(const char *status)
{
    char state[32];

    if (!status_value(status, "state", state, sizeof(state)))
        return lv_color_hex(0xffffff);
    if (strcmp(state, "connected") == 0)
        return lv_color_hex(0x7bd88f);
    if (strcmp(state, "enter_pin") == 0)
        return lv_color_hex(0xffd166);
    if (strcmp(state, "reconnecting") == 0 ||
        strcmp(state, "connecting") == 0 ||
        strcmp(state, "disconnecting") == 0)
        return lv_color_hex(0x8ecae6);
    return lv_color_hex(0xf2f2f2);
}

static void refresh_status(void)
{
    char status[160];
    char display[160];

    read_command(status, sizeof(status),
                 "cat /tmp/deneb-df-status 2>/dev/null || true");
    if (status[0] && status_label) {
        format_status_text(status, display, sizeof(display));
        lv_label_set_text(status_label, display);
        lv_obj_set_style_text_color(status_label, status_text_color(status), 0);
    }
    update_button_states(status);
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_status();
}

static void connect_cb(lv_event_t *e)
{
    char status[160];
    int refresh_only;

    (void)e;
    if (bridge_is_busy()) {
        lv_label_set_text(status_label,
                          locale_get("digital_factory.request_running"));
        return;
    }

    read_command(status, sizeof(status),
                 "cat /tmp/deneb-df-status 2>/dev/null || true");
    refresh_only = strstr(status, "state=enter_pin") != NULL ||
                   strstr(status, "state=connected") != NULL;

    system("if grep -q '^state=enter_pin\\|^state=connected' "
           "/tmp/deneb-df-status 2>/dev/null; then "
           "(/usr/bin/deneb-api digital-factory status --timeout 5 "
           ">/tmp/deneb-df-status 2>&1) & "
           "else "
           "if grep -q '^state=disconnected' /tmp/deneb-df-status 2>/dev/null; then "
           "uci -q delete ultimaker.option.cluster_id; "
           "uci -q commit ultimaker; "
           "/etc/init.d/digitalfactory stop >/dev/null 2>&1; "
           "/etc/init.d/digitalfactory disable >/dev/null 2>&1; "
           "fi; "
           "rm -f /tmp/deneb-df-status; "
           "touch /tmp/deneb-df-pair-request; "
           "(mkdir " DF_LOCK_DIR " 2>/dev/null || exit 0; "
           "/etc/init.d/digitalfactory enable >/tmp/deneb-df.log 2>&1; "
           "/etc/init.d/digitalfactory start >>/tmp/deneb-df.log 2>&1; "
           "/usr/bin/deneb-api digital-factory connect --timeout 35 "
           ">/tmp/deneb-df-status 2>&1; "
           "rmdir " DF_LOCK_DIR " 2>/dev/null) & "
           "fi");
    if (refresh_only && status[0]) {
        char display[160];
        format_status_text(status, display, sizeof(display));
        lv_label_set_text(status_label, display);
        lv_obj_set_style_text_color(status_label, status_text_color(status), 0);
    } else {
        lv_label_set_text(status_label, locale_get("digital_factory.requesting_pin"));
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xffd166), 0);
    }
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
    update_button_states("state=disconnecting");
    system("printf 'state=disconnecting\n' >/tmp/deneb-df-status; "
           "(mkdir " DF_LOCK_DIR " 2>/dev/null || exit 0; "
           "if /usr/bin/deneb-api digital-factory disconnect --timeout 10 "
           ">/tmp/deneb-df-status 2>&1; then "
           "uci -q delete ultimaker.option.cluster_id; "
           "uci -q commit ultimaker; "
           "/etc/init.d/digitalfactory stop; "
           "/etc/init.d/digitalfactory disable; "
           "printf 'state=disconnected\n' >/tmp/deneb-df-status; "
           "fi; "
           "rmdir " DF_LOCK_DIR " 2>/dev/null) >/tmp/deneb-df.log 2>&1 &");
    lv_label_set_text(status_label,
                      locale_get("digital_factory.disconnect_requested"));
}

static lv_obj_t *df_create(void)
{
    df_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(df_screen, 320, 208);
    lv_obj_align(df_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(df_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(df_screen, 0, 0);
    lv_obj_set_style_border_width(df_screen, 0, 0);
    lv_obj_set_style_pad_all(df_screen, 8, 0);
    lv_obj_set_flex_flow(df_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(df_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(df_screen, 6, 0);
    lv_obj_set_scroll_dir(df_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(df_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(df_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(df_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    info_label = lv_label_create(df_screen);
    lv_label_set_text(info_label, locale_get("digital_factory.title"));
    lv_obj_set_width(info_label, 280);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xe0e0e0), 0);

    status_label = lv_label_create(df_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_width(status_label, 280);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xf2f2f2), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_16, 0);

    connect_button = lv_button_create(df_screen);
    lv_obj_set_size(connect_button, 240, 36);
    lv_obj_set_style_bg_color(connect_button, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(connect_button, 4, 0);
    lv_obj_add_event_cb(connect_button, connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_lbl = lv_label_create(connect_button);
    lv_label_set_text(connect_lbl, locale_get("digital_factory.pair_show_pin"));
    lv_obj_center(connect_lbl);

    disconnect_button = lv_button_create(df_screen);
    lv_obj_set_size(disconnect_button, 240, 36);
    lv_obj_set_style_bg_color(disconnect_button, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(disconnect_button, 4, 0);
    lv_obj_add_event_cb(disconnect_button, disconnect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *disconnect_lbl = lv_label_create(disconnect_button);
    lv_label_set_text(disconnect_lbl, locale_get("digital_factory.disconnect"));
    lv_obj_center(disconnect_lbl);

    status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    refresh_status();

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
    connect_button = NULL;
    disconnect_button = NULL;
    disconnect_armed = 0;
}

const screen_ops_t screen_digital_factory = {
    .name = "settings.digital_factory",
    .create = df_create,
    .destroy = df_destroy,
    .show_back = true,
};
