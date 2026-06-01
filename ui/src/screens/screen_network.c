/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Network configuration screen. LVGL v9.
 * Handles both WiFi (wifi.txt) and Ethernet (eth.txt) USB import.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "wifi_setup.h"
#include "eth_setup.h"
#include "net_utils.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

static lv_obj_t *network_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *hostname_label = NULL;
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *eth_status_label = NULL;
static lv_obj_t *wifi_switch = NULL;
static lv_obj_t *import_wifi_btn = NULL;
static lv_obj_t *import_eth_btn = NULL;
static lv_obj_t *reset_eth_btn = NULL;

/* Background import state — one at a time, either wifi or eth */
static volatile int import_running = 0;
static volatile int import_cancelled = 0;
static char import_result_msg[128] = {0};
static volatile int import_done = 0;
static volatile int import_is_eth = 0;  /* 0=wifi, 1=eth */
static volatile int wifi_toggle_running = 0;
static volatile int wifi_toggle_done = 0;
static volatile int wifi_toggle_enable = 0;
static char wifi_toggle_result_msg[128] = {0};

static void set_status_text(const char *text)
{
    if (!status_label)
        return;
    lv_label_set_text(status_label, text ? text : "");
    if (text && text[0])
        lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------------ */
/* Background import thread                                            */
/* ------------------------------------------------------------------ */

static void *import_thread(void *arg)
{
    (void)arg;
    if (import_is_eth)
        eth_setup_import(import_result_msg, sizeof(import_result_msg));
    else
        wifi_setup_import(import_result_msg, sizeof(import_result_msg));
    import_done = 1;
    import_running = 0;
    return NULL;
}

static void *wifi_toggle_thread(void *arg)
{
    (void)arg;
    wifi_result_t res = wifi_setup_set_enabled(wifi_toggle_enable != 0);

    if (res == WIFI_OK) {
        snprintf(wifi_toggle_result_msg, sizeof(wifi_toggle_result_msg),
                 "%s", wifi_toggle_enable ?
                 locale_get("network.wifi_enabled") :
                 locale_get("network.wifi_disabled"));
    } else if (res == WIFI_ERR_NO_SSID) {
        snprintf(wifi_toggle_result_msg, sizeof(wifi_toggle_result_msg),
                 "%s", locale_get("network.wifi_no_config"));
    } else {
        snprintf(wifi_toggle_result_msg, sizeof(wifi_toggle_result_msg),
                 "%s", locale_get("network.wifi_toggle_failed"));
    }

    wifi_toggle_done = 1;
    wifi_toggle_running = 0;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* UI refresh                                                          */
/* ------------------------------------------------------------------ */

static void refresh_wifi_status(void)
{
    if (!wifi_status_label)
        return;
    char status[128];
    wifi_setup_get_status(status, sizeof(status));
    lv_label_set_text(wifi_status_label, status);
}

static void refresh_eth_status(void)
{
    if (!eth_status_label)
        return;
    char status[128];
    eth_setup_get_status(status, sizeof(status));
    lv_label_set_text(eth_status_label, status);
}

static void refresh_wifi_switch(void)
{
    if (!wifi_switch)
        return;

    if (wifi_setup_is_enabled())
        lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(wifi_switch, LV_STATE_CHECKED);

    if (wifi_toggle_running || import_running || !wifi_setup_is_configured())
        lv_obj_add_state(wifi_switch, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(wifi_switch, LV_STATE_DISABLED);
}

static void refresh_buttons(void)
{
    int wifi_cfg = wifi_setup_is_configured();
    int eth_static = eth_setup_is_static();
    int busy = import_running || wifi_toggle_running;

    if (reset_eth_btn) {
        if (eth_static)
            lv_obj_remove_flag(reset_eth_btn, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(reset_eth_btn, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *buttons[] = {
        import_wifi_btn, import_eth_btn, reset_eth_btn
    };
    for (unsigned int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (!buttons[i])
            continue;
        if (busy)
            lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
        else
            lv_obj_remove_state(buttons[i], LV_STATE_DISABLED);
    }

    refresh_wifi_switch();
}

/* ------------------------------------------------------------------ */
/* Timer callback for polling import result                            */
/* ------------------------------------------------------------------ */

static void import_poll_timer_cb(lv_timer_t *timer)
{
    if (import_cancelled) {
        lv_timer_delete(timer);
        return;
    }
    if (import_done) {
        import_done = 0;
        set_status_text(import_result_msg);
        refresh_wifi_status();
        refresh_eth_status();
        refresh_buttons();
        lv_timer_delete(timer);
    }
}

static void wifi_toggle_poll_timer_cb(lv_timer_t *timer)
{
    if (wifi_toggle_done) {
        wifi_toggle_done = 0;
        set_status_text(wifi_toggle_result_msg);
        refresh_wifi_status();
        refresh_eth_status();
        refresh_buttons();
        lv_timer_delete(timer);
    }
}

/* ------------------------------------------------------------------ */
/* Button callbacks — WiFi                                             */
/* ------------------------------------------------------------------ */

static void import_wifi_cb(lv_event_t *e)
{
    (void)e;
    if (import_running || wifi_toggle_running) {
        set_status_text(locale_get("network.import_running"));
        return;
    }
    char path[160];
    wifi_result_t res = wifi_setup_find_config(path, sizeof(path));
    if (res == WIFI_ERR_NO_USB) {
        set_status_text(locale_get("network.no_usb"));
        return;
    }
    if (res == WIFI_ERR_NO_FILE) {
        set_status_text(locale_get("network.no_wifi_txt"));
        return;
    }
    set_status_text(locale_get("network.importing"));
    import_running = 1;
    import_done = 0;
    import_cancelled = 0;
    import_is_eth = 0;
    import_result_msg[0] = '\0';
    pthread_t tid;
    if (pthread_create(&tid, NULL, import_thread, NULL) != 0) {
        import_running = 0;
        set_status_text(locale_get("network.import_start_failed"));
        return;
    }
    pthread_detach(tid);
    lv_timer_create(import_poll_timer_cb, 200, NULL);
}

/* ------------------------------------------------------------------ */
/* Button callbacks — Ethernet                                         */
/* ------------------------------------------------------------------ */

static void import_eth_cb(lv_event_t *e)
{
    (void)e;
    if (import_running || wifi_toggle_running) {
        set_status_text(locale_get("network.import_running"));
        return;
    }
    char path[160];
    eth_result_t res = eth_setup_find_config(path, sizeof(path));
    if (res == ETH_ERR_NO_USB) {
        set_status_text(locale_get("network.no_usb"));
        return;
    }
    if (res == ETH_ERR_NO_FILE) {
        set_status_text(locale_get("network.no_eth_txt"));
        return;
    }
    set_status_text(locale_get("network.importing_eth"));
    import_running = 1;
    import_done = 0;
    import_cancelled = 0;
    import_is_eth = 1;
    import_result_msg[0] = '\0';
    pthread_t tid;
    if (pthread_create(&tid, NULL, import_thread, NULL) != 0) {
        import_running = 0;
        set_status_text(locale_get("network.import_start_failed"));
        return;
    }
    pthread_detach(tid);
    lv_timer_create(import_poll_timer_cb, 200, NULL);
}

static void reset_eth_cb(lv_event_t *e)
{
    (void)e;
    eth_setup_clear();
    set_status_text(locale_get("network.eth_reset"));
    refresh_eth_status();
    refresh_buttons();
}

static void wifi_switch_cb(lv_event_t *e)
{
    (void)e;
    if (!wifi_switch)
        return;

    if (import_running || wifi_toggle_running) {
        set_status_text(locale_get("network.import_running"));
        refresh_wifi_switch();
        return;
    }

    int enable = lv_obj_has_state(wifi_switch, LV_STATE_CHECKED);
    if (enable && !wifi_setup_is_configured()) {
        set_status_text(locale_get("network.wifi_no_config"));
        refresh_wifi_switch();
        return;
    }

    set_status_text(locale_get(enable ? "network.wifi_turning_on" :
                                        "network.wifi_turning_off"));
    wifi_toggle_enable = enable;
    wifi_toggle_running = 1;
    wifi_toggle_done = 0;
    wifi_toggle_result_msg[0] = '\0';
    refresh_buttons();

    pthread_t tid;
    if (pthread_create(&tid, NULL, wifi_toggle_thread, NULL) != 0) {
        wifi_toggle_running = 0;
        set_status_text(locale_get("network.wifi_toggle_failed"));
        refresh_buttons();
        return;
    }
    pthread_detach(tid);
    lv_timer_create(wifi_toggle_poll_timer_cb, 200, NULL);
}

/* ------------------------------------------------------------------ */
/* Screen create/destroy                                               */
/* ------------------------------------------------------------------ */

static void create_status_row(lv_obj_t *parent, const char *label_text,
                              lv_obj_t **value_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 300, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(row, 6, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x8080ff), 0);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);

    *value_out = lv_label_create(row);
    lv_obj_set_style_text_color(*value_out, lv_color_hex(0x80ff80), 0);
    lv_obj_set_style_text_font(*value_out, &deneb_font_12, 0);
}

static void create_action_btn(lv_obj_t *parent, const char *text,
                              lv_event_cb_t cb, lv_obj_t **btn_out)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 300, 30);
    lv_obj_set_style_margin_top(btn, 3, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    if (btn_out)
        *btn_out = btn;
}

static void create_wifi_switch(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 300, 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_margin_top(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, locale_get("network.wifi_power"));
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);

    wifi_switch = lv_switch_create(row);
    lv_obj_set_size(wifi_switch, 52, 28);
    lv_obj_add_event_cb(wifi_switch, wifi_switch_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);
    refresh_wifi_switch();
}

static lv_obj_t *network_create(void)
{
    char name[96];

    net_read_command(name, sizeof(name),
                     "uci -q get system.@system[0].hostname 2>/dev/null || "
                     "uci -q get ultimaker.system.printer_name 2>/dev/null || "
                     "cat /proc/sys/kernel/hostname");

    network_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(network_screen, 320, 208);
    lv_obj_align(network_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(network_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(network_screen, 0, 0);
    lv_obj_set_style_border_width(network_screen, 0, 0);
    lv_obj_set_style_pad_all(network_screen, 8, 0);
    lv_obj_set_flex_flow(network_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(network_screen, 3, 0);
    lv_obj_set_scroll_dir(network_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(network_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(network_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(network_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    create_status_row(network_screen, locale_get("network.hostname_label"),
                      &hostname_label);
    lv_label_set_text(hostname_label,
                      name[0] ? name : locale_get("diagnostics.unknown"));
    lv_label_set_long_mode(hostname_label, LV_LABEL_LONG_MODE_DOTS);

    /* WiFi status row */
    create_status_row(network_screen, locale_get("network.wifi_label"),
                      &wifi_status_label);
    refresh_wifi_status();

    /* Ethernet status row */
    create_status_row(network_screen, locale_get("network.eth_label"),
                      &eth_status_label);
    refresh_eth_status();

    create_wifi_switch(network_screen);

    create_action_btn(network_screen,
                      locale_get("network.import_wifi"),
                      import_wifi_cb, &import_wifi_btn);
    create_action_btn(network_screen,
                      locale_get("network.import_eth"),
                      import_eth_cb, &import_eth_btn);

    /* Ethernet maintenance */
    create_action_btn(network_screen,
                      locale_get("network.reset_eth"),
                      reset_eth_cb, &reset_eth_btn);

    status_label = lv_label_create(network_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_width(status_label, 300);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);

    refresh_buttons();

    return network_screen;
}

static void network_destroy(void)
{
    if (import_running) {
        import_cancelled = 1;
        for (int i = 0; i < 30 && import_running; i++)
            usleep(100000);
    }
    import_cancelled = 0;
    network_screen = NULL;
    status_label = NULL;
    hostname_label = NULL;
    wifi_status_label = NULL;
    eth_status_label = NULL;
    wifi_switch = NULL;
    import_wifi_btn = NULL;
    import_eth_btn = NULL;
    reset_eth_btn = NULL;
}

const screen_ops_t screen_network = {
    .name = "settings.network",
    .create = network_create,
    .destroy = network_destroy,
    .show_back = true,
};
