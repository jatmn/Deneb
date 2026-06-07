/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Network print configuration conflict prompt.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PENDING_JOB_PATH "/tmp/deneb-cluster-print-job.json"

static lv_obj_t *conflict_screen = NULL;
static lv_obj_t *message_label = NULL;
static lv_obj_t *detail_label = NULL;
static lv_obj_t *status_label = NULL;
static int current_tracker = -1;

static int json_read_file(char *buf, size_t buf_sz)
{
    FILE *f = fopen(PENDING_JOB_PATH, "rb");
    if (!f) return -1;

    size_t n = fread(buf, 1, buf_sz - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n > 0 ? 0 : -1;
}

static int json_get_string(const char *json, const char *key,
                           char *out, size_t out_sz)
{
    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(json, needle);
    if (!p) return -1;
    p = strchr(p + strlen(needle), ':');
    if (!p) return -1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return -1;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        if (*p == '\\' && p[1])
            p++;
        out[i++] = *p++;
    }
    if (*p != '"') return -1;
    out[i] = '\0';
    return 0;
}

static int json_get_int(const char *json, const char *key)
{
    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(json, needle);
    if (!p) return -1;
    p = strchr(p + strlen(needle), ':');
    if (!p) return -1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!isdigit((unsigned char)*p)) return -1;
    return atoi(p);
}

int print_conflict_has_pending(void)
{
    char buf[2048];
    if (json_read_file(buf, sizeof(buf)) < 0)
        return 0;
    return strstr(buf, "\"configuration_changes_required\"") != NULL &&
           (strstr(buf, "\"material_change\"") != NULL ||
            strstr(buf, "\"print_core_change\"") != NULL) &&
           json_get_int(buf, "deneb_tracker") >= 0;
}

static int send_stock_instruction(const char *instruction)
{
    if (current_tracker < 0)
        return -1;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "PYTHONPATH=/home:/home/lib python3 -c \""
             "import sys,time;"
             "from sponge.spinner import spinner;"
             "from sponge.ipc import zmqipc;"
             "import gershwin.constructs as gershwin;"
             "import gershwin.node as node;"
             "from gershwin.manager import Manager;"
             "from cygnus.marshal.types.printing import PrintHandlingRequest,PrintHandlingInstruction;"
             "result={'done':False,'reply':None}\n"
             "@gershwin.node('client')\n"
             "class ClientNode(node.Node):\n"
             " def plan(self):\n"
             "  self.doOption(gershwin.DO_OPTIONS.DO_IMMEDIATELY)\n"
             "  self.addStep(goal='pending print action', func=self._run)\n"
             " def _run(self):\n"
             "  i=PrintHandlingInstruction.%s\n"
             "  r=PrintHandlingRequest.create(tracker=int(sys.argv[1]), instruction=i, options={})\n"
             "  result['reply']=yield from self.call('coordinator::print::handling', r)\n"
             "  result['done']=True\n"
             "m=Manager('deneb-ui-print-action', spinner.Spinner, zmqipc.ZMQIPC, ip='tcp://127.0.0.1:', pubbase=5546, pubinstance=3);"
             "m.addNode(ClientNode.id, ClientNode);"
             "\nfor _ in range(30):\n"
             " m.spinner.spin(100)\n"
             " m.run_time=m.spinner.getElapsed()\n"
             " m.spin()\n"
             " if result['done']:\n"
             "  break\n"
             "reply=result.get('reply') or {}\n"
             "sys.exit(0 if result['done'] and reply.get('accepted') else 1)\n"
             "\" %d >/tmp/deneb-print-action.log 2>&1",
             instruction, current_tracker);

    return system(cmd);
}

static void finish_prompt(const char *status_key)
{
    unlink(PENDING_JOB_PATH);
    if (status_label)
        lv_label_set_text(status_label, locale_get(status_key));
    screen_mgr_pop();
}

static void continue_btn_cb(lv_event_t *e)
{
    (void)e;
    if (status_label)
        lv_label_set_text(status_label, locale_get("print_conflict.continuing"));
    if (send_stock_instruction("PREPARE") == 0)
        finish_prompt("print_conflict.continuing");
    else if (status_label)
        lv_label_set_text(status_label, locale_get("print_conflict.action_failed"));
}

static void cancel_btn_cb(lv_event_t *e)
{
    (void)e;
    if (status_label)
        lv_label_set_text(status_label, locale_get("print_conflict.cancelled"));
    if (send_stock_instruction("ABORT") == 0)
        finish_prompt("print_conflict.cancelled");
    else if (status_label)
        lv_label_set_text(status_label, locale_get("print_conflict.action_failed"));
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             lv_event_cb_t cb, lv_color_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 138, 34);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_center(lbl);
    return btn;
}

static void load_prompt_text(void)
{
    char buf[4096];
    char job[96] = "network print";
    char loaded[96] = "loaded material";
    char wanted[96] = "sliced material";
    char msg[256];
    char detail[160];

    current_tracker = -1;
    if (json_read_file(buf, sizeof(buf)) == 0) {
        json_get_string(buf, "name", job, sizeof(job));
        json_get_string(buf, "origin_name", loaded, sizeof(loaded));
        json_get_string(buf, "target_name", wanted, sizeof(wanted));
        current_tracker = json_get_int(buf, "deneb_tracker");
    }

    snprintf(msg, sizeof(msg), locale_get("print_conflict.message_fmt"),
             wanted, loaded);
    snprintf(detail, sizeof(detail), locale_get("print_conflict.job_fmt"), job);

    lv_label_set_text(message_label, msg);
    lv_label_set_text(detail_label, detail);
    lv_label_set_text(status_label, current_tracker >= 0 ? "" :
                      locale_get("print_conflict.action_failed"));
}

static lv_obj_t *conflict_create(void)
{
    conflict_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(conflict_screen, 320, 208);
    lv_obj_align(conflict_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(conflict_screen, lv_color_hex(0x151515), 0);
    lv_obj_set_style_radius(conflict_screen, 0, 0);
    lv_obj_set_style_border_width(conflict_screen, 0, 0);
    lv_obj_set_style_pad_all(conflict_screen, 10, 0);
    lv_obj_set_flex_flow(conflict_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(conflict_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(conflict_screen, 7, 0);

    lv_obj_t *icon = lv_label_create(conflict_screen);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xffc924), 0);
    lv_obj_set_style_text_font(icon, &deneb_font_16, 0);

    message_label = lv_label_create(conflict_screen);
    lv_obj_set_width(message_label, 292);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(0xf2f2f2), 0);
    lv_obj_set_style_text_font(message_label, &deneb_font_12, 0);

    detail_label = lv_label_create(conflict_screen);
    lv_obj_set_width(detail_label, 292);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(detail_label, lv_color_hex(0xa8b0bd), 0);
    lv_obj_set_style_text_font(detail_label, &deneb_font_12, 0);

    lv_obj_t *actions = lv_obj_create(conflict_screen);
    lv_obj_set_size(actions, 300, 40);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_button(actions, locale_get("print_conflict.cancel"), cancel_btn_cb,
                lv_color_hex(0x4a4f59));
    make_button(actions, locale_get("print_conflict.continue"), continue_btn_cb,
                lv_color_hex(0x1d5fd3));

    status_label = lv_label_create(conflict_screen);
    lv_obj_set_width(status_label, 292);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa8b0bd), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    load_prompt_text();
    return conflict_screen;
}

static void conflict_destroy(void)
{
    conflict_screen = NULL;
    message_label = NULL;
    detail_label = NULL;
    status_label = NULL;
    current_tracker = -1;
}

const screen_ops_t screen_print_conflict = {
    .name = "print_conflict.title",
    .create = conflict_create,
    .destroy = conflict_destroy,
    .show_back = false,
};
