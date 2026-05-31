/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Network information screen.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *network_screen = NULL;
static lv_obj_t *status_label = NULL;

static void connect_wifi_cb(lv_event_t *e)
{
    (void)e;
    system("(uci set wireless.ap.disabled=0; uci commit wireless; "
           "wifi; "
           "PYTHONPATH=/home python3 /home/cygnus/wificonnect/server.py & "
           "/etc/init.d/dnsmasq start; "
           "/etc/init.d/nodogsplash start) "
           ">/tmp/deneb-wifi-connect.log 2>&1 &");
    lv_label_set_text(status_label, locale_get("network.connect_starting"));
}

static void disconnect_wifi_cb(lv_event_t *e)
{
    (void)e;
    system("(uci set wireless.ap.disabled=1; uci set wireless.ap.hidden=0; "
           "uci commit wireless; wifisetup clear; wifi) "
           ">/tmp/deneb-wifi-disconnect.log 2>&1 &");
    lv_label_set_text(status_label, locale_get("network.disconnect_requested"));
}

static void read_command(char *dst, size_t dst_size, const char *cmd)
{
    if (dst_size == 0)
        return;
    dst[0] = '\0';

    FILE *fp = popen(cmd, "r");
    if (!fp)
        return;

    size_t used = 0;
    while (used + 1 < dst_size && fgets(dst + used, dst_size - used, fp)) {
        used = strlen(dst);
    }
    pclose(fp);

    while (used > 0 && (dst[used - 1] == '\n' || dst[used - 1] == '\r'))
        dst[--used] = '\0';
}

static lv_obj_t *network_create(void)
{
    char info[640];
    char name[96];
    char ips[256];
    char wifi[128];

    read_command(name, sizeof(name),
                 "uci -q get system.@system[0].hostname 2>/dev/null || "
                 "uci -q get ultimaker.system.printer_name 2>/dev/null || "
                 "cat /proc/sys/kernel/hostname");
    read_command(ips, sizeof(ips),
                 "ip -4 addr show | awk '/inet / {print $2 \" \" $NF}'");
    read_command(wifi, sizeof(wifi),
                 "uci -q get wireless.@wifi-iface[0].ssid 2>/dev/null || "
                 "iwgetid -r 2>/dev/null || echo unknown");

    snprintf(info, sizeof(info), "%s\n\n%s\n%s\n\n%s\n%s",
             locale_get("network.title"),
             locale_get("network.printer"),
             name[0] ? name : "unknown",
             locale_get("network.addresses"),
             ips[0] ? ips : wifi);

    network_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(network_screen, 320, 208);
    lv_obj_align(network_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(network_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(network_screen, 0, 0);
    lv_obj_set_style_border_width(network_screen, 0, 0);
    lv_obj_set_style_pad_all(network_screen, 10, 0);
    lv_obj_set_flex_flow(network_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(network_screen, 8, 0);
    lv_obj_set_scroll_dir(network_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(network_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(network_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(network_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    lv_obj_t *lbl = lv_label_create(network_screen);
    lv_label_set_text(lbl, info);
    lv_obj_set_width(lbl, 300);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);

    lv_obj_t *connect_btn = lv_button_create(network_screen);
    lv_obj_set_size(connect_btn, 292, 32);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(connect_btn, 4, 0);
    lv_obj_add_event_cb(connect_btn, connect_wifi_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, locale_get("network.connect_wifi"));
    lv_obj_center(connect_lbl);

    lv_obj_t *disconnect_btn = lv_button_create(network_screen);
    lv_obj_set_size(disconnect_btn, 292, 32);
    lv_obj_set_style_bg_color(disconnect_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(disconnect_btn, 4, 0);
    lv_obj_add_event_cb(disconnect_btn, disconnect_wifi_cb, LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *disconnect_lbl = lv_label_create(disconnect_btn);
    lv_label_set_text(disconnect_lbl, locale_get("network.disconnect_wifi"));
    lv_obj_center(disconnect_lbl);

    status_label = lv_label_create(network_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_width(status_label, 292);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    return network_screen;
}

static void network_destroy(void)
{
    network_screen = NULL;
    status_label = NULL;
}

const screen_ops_t screen_network = {
    .name = "settings.network",
    .create = network_create,
    .destroy = network_destroy,
    .show_back = true,
};
