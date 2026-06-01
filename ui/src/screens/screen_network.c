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
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *eth_status_label = NULL;
static lv_obj_t *import_wifi_btn = NULL;
static lv_obj_t *disconnect_wifi_btn = NULL;
static lv_obj_t *import_eth_btn = NULL;
static lv_obj_t *reset_eth_btn = NULL;

/* Background import state — one at a time, either wifi or eth */
static volatile int import_running = 0;
static volatile int import_cancelled = 0;
static char import_result_msg[128] = {0};
static volatile int import_done = 0;
static volatile int import_is_eth = 0;  /* 0=wifi, 1=eth */

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

static void refresh_buttons(void)
{
    int wifi_cfg = wifi_setup_is_configured();
    int eth_static = eth_setup_is_static();

    if (disconnect_wifi_btn) {
        if (wifi_cfg)
            lv_obj_remove_flag(disconnect_wifi_btn, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(disconnect_wifi_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (reset_eth_btn) {
        if (eth_static)
            lv_obj_remove_flag(reset_eth_btn, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(reset_eth_btn, LV_OBJ_FLAG_HIDDEN);
    }
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
        if (status_label)
            lv_label_set_text(status_label, import_result_msg);
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
    if (import_running) {
        lv_label_set_text(status_label,
                          locale_get("network.import_running"));
        return;
    }
    char path[160];
    wifi_result_t res = wifi_setup_find_config(path, sizeof(path));
    if (res == WIFI_ERR_NO_USB) {
        lv_label_set_text(status_label,
                          locale_get("network.no_usb"));
        return;
    }
    if (res == WIFI_ERR_NO_FILE) {
        lv_label_set_text(status_label,
                          locale_get("network.no_wifi_txt"));
        return;
    }
    lv_label_set_text(status_label,
                      locale_get("network.importing"));
    import_running = 1;
    import_done = 0;
    import_cancelled = 0;
    import_is_eth = 0;
    import_result_msg[0] = '\0';
    pthread_t tid;
    if (pthread_create(&tid, NULL, import_thread, NULL) != 0) {
        import_running = 0;
        lv_label_set_text(status_label, "Failed to start import");
        return;
    }
    pthread_detach(tid);
    lv_timer_create(import_poll_timer_cb, 200, NULL);
}

static void disconnect_wifi_cb(lv_event_t *e)
{
    (void)e;
    wifi_setup_clear();
    lv_label_set_text(status_label,
                      locale_get("network.wifi_disconnected"));
    refresh_wifi_status();
    refresh_buttons();
}

/* ------------------------------------------------------------------ */
/* Button callbacks — Ethernet                                         */
/* ------------------------------------------------------------------ */

static void import_eth_cb(lv_event_t *e)
{
    (void)e;
    if (import_running) {
        lv_label_set_text(status_label,
                          locale_get("network.import_running"));
        return;
    }
    char path[160];
    eth_result_t res = eth_setup_find_config(path, sizeof(path));
    if (res == ETH_ERR_NO_USB) {
        lv_label_set_text(status_label,
                          locale_get("network.no_usb"));
        return;
    }
    if (res == ETH_ERR_NO_FILE) {
        lv_label_set_text(status_label,
                          locale_get("network.no_eth_txt"));
        return;
    }
    lv_label_set_text(status_label,
                      locale_get("network.importing_eth"));
    import_running = 1;
    import_done = 0;
    import_cancelled = 0;
    import_is_eth = 1;
    import_result_msg[0] = '\0';
    pthread_t tid;
    if (pthread_create(&tid, NULL, import_thread, NULL) != 0) {
        import_running = 0;
        lv_label_set_text(status_label, "Failed to start import");
        return;
    }
    pthread_detach(tid);
    lv_timer_create(import_poll_timer_cb, 200, NULL);
}

static void reset_eth_cb(lv_event_t *e)
{
    (void)e;
    eth_setup_clear();
    lv_label_set_text(status_label,
                      locale_get("network.eth_reset"));
    refresh_eth_status();
    refresh_buttons();
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
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    if (btn_out)
        *btn_out = btn;
}

static lv_obj_t *network_create(void)
{
    char info[640];
    char name[96];
    char ips[256];

    net_read_command(name, sizeof(name),
                     "uci -q get system.@system[0].hostname 2>/dev/null || "
                     "uci -q get ultimaker.system.printer_name 2>/dev/null || "
                     "cat /proc/sys/kernel/hostname");
    net_read_command(ips, sizeof(ips),
                     "ip -4 addr show | awk '/inet / {print $2 \" \" $NF}'");

    snprintf(info, sizeof(info), "%s\n%s\n%s",
             locale_get("network.printer"),
             name[0] ? name : "unknown",
             ips[0] ? ips : "");

    network_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(network_screen, 320, 208);
    lv_obj_align(network_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(network_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(network_screen, 0, 0);
    lv_obj_set_style_border_width(network_screen, 0, 0);
    lv_obj_set_style_pad_all(network_screen, 10, 0);
    lv_obj_set_flex_flow(network_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(network_screen, 4, 0);
    lv_obj_set_scroll_dir(network_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(network_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(network_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(network_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    /* Network info */
    lv_obj_t *lbl = lv_label_create(network_screen);
    lv_label_set_text(lbl, info);
    lv_obj_set_width(lbl, 300);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);

    /* WiFi status row */
    create_status_row(network_screen, locale_get("network.wifi_label"),
                      &wifi_status_label);
    refresh_wifi_status();

    /* WiFi buttons */
    create_action_btn(network_screen,
                      locale_get("network.import_wifi"),
                      import_wifi_cb, &import_wifi_btn);
    create_action_btn(network_screen,
                      locale_get("network.disconnect_wifi"),
                      disconnect_wifi_cb, &disconnect_wifi_btn);

    /* Ethernet status row */
    create_status_row(network_screen, locale_get("network.eth_label"),
                      &eth_status_label);
    refresh_eth_status();

    /* Ethernet buttons */
    create_action_btn(network_screen,
                      locale_get("network.import_eth"),
                      import_eth_cb, &import_eth_btn);
    create_action_btn(network_screen,
                      locale_get("network.reset_eth"),
                      reset_eth_cb, &reset_eth_btn);

    refresh_buttons();

    /* Status message label */
    status_label = lv_label_create(network_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_width(status_label, 300);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

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
    wifi_status_label = NULL;
    eth_status_label = NULL;
    import_wifi_btn = NULL;
    disconnect_wifi_btn = NULL;
    import_eth_btn = NULL;
    reset_eth_btn = NULL;
}

const screen_ops_t screen_network = {
    .name = "settings.network",
    .create = network_create,
    .destroy = network_destroy,
    .show_back = true,
};
