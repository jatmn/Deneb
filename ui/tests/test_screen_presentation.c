/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include "backend_comm.h"
#include "locale.h"
#include "manual_operation.h"
#include "screen_mgr.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern const screen_ops_t screen_print;
extern const screen_ops_t screen_material;
extern const screen_ops_t screen_material_workflow;
extern const screen_ops_t screen_language;

const screen_ops_t screen_set_material = {0};
lv_font_t deneb_font_12;
lv_font_t deneb_font_14;
static printer_state_t observed;
static unsigned state_reads, saved_languages, cancel_requests;

const printer_state_t *backend_get_state(void) { state_reads++; return &observed; }
int backend_has_active_print_context(void) { return observed.is_printing; }
int backend_is_ready(void) { return 1; }
int backend_manual_action_allowed(void) { return !observed.is_printing; }
int backend_print_start_allowed(void) { return 0; }
int backend_is_stop_print_inflight(void) { return 0; }
int backend_pause_print(void) { return -1; }
int backend_resume_print(void) { return -1; }
int backend_stop_print(void) { return -1; }
int backend_send_job(const char *path, const char *source, const char *uuid,
                     float bed_target, float head_target)
{
    (void)path;
    (void)source;
    (void)uuid;
    (void)bed_target;
    (void)head_target;
    return -1;
}

int backend_send_gcode(const char *gcode)
{
    assert(strncmp(gcode, "M104 S", 6) == 0);
    return 0;
}

int backend_send_gcodes(const char *const *gcodes, size_t count)
{
    assert(gcodes && count == 2);
    observed.manual_id++;
    observed.manual_state = DENEB_MANUAL_RUNNING;
    return 0;
}

uint64_t backend_get_last_operation(void) { return observed.manual_id; }

int backend_cancel_manual(uint64_t operation)
{
    assert(operation == observed.manual_id);
    cancel_requests++;
    observed.manual_state = DENEB_MANUAL_CANCELLING;
    return 0;
}

int __wrap_system(const char *command)
{
    assert(strstr(command, "deneb.system.language='nl'"));
    saved_languages++;
    return 0;
}

static lv_obj_t *root_create(void) { return lv_obj_create(lv_screen_active()); }
static const screen_ops_t root_screen = { .name = "root", .create = root_create };

static void flush_frame(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static lv_obj_t *find_label(lv_obj_t *object, const char *text)
{
    if (lv_obj_is_hidden(object))
        return NULL;
    if (lv_obj_check_type(object, &lv_label_class) &&
        strcmp(lv_label_get_text(object), text) == 0)
        return object;
    for (uint32_t index = 0; index < lv_obj_get_child_count(object); index++) {
        lv_obj_t *label = find_label(lv_obj_get_child(object, index), text);
        if (label)
            return label;
    }
    return NULL;
}

static lv_obj_t *visible_button(const char *text)
{
    lv_obj_t *label = find_label(lv_screen_active(), text);
    assert(label);
    lv_obj_t *button = lv_obj_get_parent(label);
    assert(lv_obj_check_type(button, &lv_button_class));
    return button;
}

static void click(const char *text)
{
    lv_obj_t *button = visible_button(text);
    assert(!lv_obj_has_state(button, LV_STATE_DISABLED));
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
}

static unsigned tick_count(unsigned steps)
{
    unsigned initial = state_reads;
    for (unsigned i = 0; i < steps; i++) {
        lv_tick_inc(500);
        lv_timer_handler();
    }
    return state_reads - initial;
}

int main(int argc, char **argv)
{
    bool measure_only = argc == 3 && strcmp(argv[2], "--measure") == 0;
    assert((argc == 2 || measure_only) && chdir(argv[1]) == 0);
    static uint16_t draw_buffer[320 * 40];
    lv_init();
    deneb_font_12 = lv_font_montserrat_12;
    deneb_font_14 = lv_font_montserrat_14;
    lv_display_t *display = lv_display_create(320, 240);
    assert(display && locale_init("en") == 0);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_frame);
    screen_mgr_init();
    screen_mgr_push(&root_screen);

    observed.connected = true;
    observed.is_printing = true;
    observed.time_left = 120;
    screen_mgr_push(&screen_print);
    unsigned visible = tick_count(8);
    assert(find_label(lv_screen_active(), "Left 2 min"));

    click(locale_get("menu.material"));
    assert(strcmp(screen_mgr_current_name(), screen_material.name) == 0);
    observed.time_left = 240;
    unsigned covered = tick_count(8);
    observed.time_left = 300;
    click(LV_SYMBOL_LEFT);
    bool immediate = find_label(lv_screen_active(), "Left 5 min") != NULL;
    if (!measure_only) {
        assert(visible == 8 && covered == 0 && immediate);
        assert(tick_count(1) == 1);
    }

    screen_mgr_push(&screen_language);
    unsigned before = state_reads;
    click(locale_get("language.nl"));
    unsigned rebuild = state_reads - before;
    assert(saved_languages == 1 && strcmp(locale_current(), "nl") == 0);
    observed.time_left = 360;
    unsigned rebuilt_covered = tick_count(8);
    observed.time_left = 420;
    click(LV_SYMBOL_LEFT);
    bool rebuilt_immediate = find_label(lv_screen_active(), "Left 7 min") != NULL;
    if (!measure_only) {
        assert(rebuild == 0 && rebuilt_covered == 0 && rebuilt_immediate);
        assert(find_label(lv_screen_active(), locale_get(screen_print.name)));
        assert(tick_count(1) == 1);
    }
    printf("Print backend reads in 4 simulated seconds: visible=%u covered=%u rebuilt_covered=%u; "
           "rebuild_reads=%u immediate=%d rebuilt_immediate=%d\n",
           visible, covered, rebuilt_covered, rebuild, immediate, rebuilt_immediate);

    observed.is_printing = false;
    observed.nozzle_temp_cur = 239.0f;
    click(locale_get("menu.material"));
    click(locale_get("material.load_change"));
    assert(strcmp(screen_mgr_current_name(), screen_material_workflow.name) == 0);
    lv_obj_t *load = visible_button(locale_get("material.load_short"));
    click(locale_get("material.load_short"));
    assert(lv_obj_has_state(load, LV_STATE_DISABLED));
    screen_mgr_push(&screen_language);
    observed.manual_state = DENEB_MANUAL_COMPLETE;
    tick_count(1);
    assert(!lv_obj_has_state(load, LV_STATE_DISABLED));
    click(LV_SYMBOL_LEFT);

    click(locale_get("material.load_short"));
    click(locale_get("material.stop"));
    assert(cancel_requests == 1 && lv_obj_has_state(load, LV_STATE_DISABLED));
    screen_mgr_push(&screen_language);
    observed.manual_state = DENEB_MANUAL_CANCELLED;
    observed.nozzle_temp_cur = 100.0f;
    tick_count(1);
    click(LV_SYMBOL_LEFT);
    assert(find_label(lv_screen_active(), locale_get("material.set_target")));
    assert(cancel_requests == 1);
    click(LV_SYMBOL_LEFT);

    observed.nozzle_temp_cur = 239.0f;
    click(locale_get("material.load_change"));
    load = visible_button(locale_get("material.load_short"));
    click(locale_get("material.load_short"));
    screen_mgr_push(&screen_language);
    lv_tick_inc(DENEB_MANUAL_TIMEOUT_MS + 9500U);
    lv_timer_handler();
    assert(lv_obj_has_state(load, LV_STATE_DISABLED));
    tick_count(1);
    click(LV_SYMBOL_LEFT);
    assert(find_label(lv_screen_active(), locale_get("material.move_unconfirmed")));
    assert(cancel_requests == 1);
    puts("Covered Material workflow: completion, timeout and cancellation verified");

    screen_mgr_pop();
    screen_mgr_pop();
    screen_mgr_pop();
    assert(screen_mgr_depth() == 1);
    lv_display_delete(display);
    locale_deinit();
    lv_deinit();
    return 0;
}
