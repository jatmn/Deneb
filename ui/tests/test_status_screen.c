/* SPDX-License-Identifier: MPL-2.0 */
#include "../src/screens/screen_status.c"

#include <assert.h>
#include <string.h>

static printer_state_t observed;
static bool translated;
static unsigned pauses;
static unsigned resumes;
static unsigned stops;
static unsigned flushes;

const printer_state_t *backend_get_state(void) { return &observed; }
int backend_get_print_display_name(char *out, size_t size)
{
    snprintf(out, size, "%s", observed.filename);
    return out[0] ? 0 : -1;
}
int backend_has_print_name(const char *name) { return name[0] != '\0'; }
int backend_has_active_print_context(void)
{
    return observed.is_printing || observed.is_paused;
}
int backend_has_preparing_print_context(void) { return 0; }
int backend_has_stoppable_print_context(void) { return observed.is_printing; }
int backend_has_abort_print_context(void) { return stops != 0; }
int backend_is_stop_print_inflight(void) { return stops != 0; }
void backend_clear_print_display_context_if_idle(void) {}
int backend_pause_print(void) { pauses++; return 0; }
int backend_resume_print(void) { resumes++; return 0; }
int backend_stop_print(void) { stops++; return 0; }

const char *locale_get(const char *key)
{
    if (translated && strcmp(key, "status.preparing") == 0)
        return "Verbinden";
    if (translated && strcmp(key, "status.no_file") == 0)
        return "Geen bestand";
    if (translated && strcmp(key, "status.idle") == 0)
        return "Gereed";
    return key;
}

static void flush_frame(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    flushes++;
    lv_display_flush_ready(display);
}

static void tick(lv_display_t *display)
{
    lv_tick_inc(250);
    lv_timer_handler();
    lv_refr_now(display);
}

static void assert_label(lv_obj_t *label, const char *text)
{
    assert(strcmp(lv_label_get_text(label), text) == 0);
}

static void assert_steady(lv_display_t *display)
{
    /* Finish any style transition from entering this state before sampling. */
    tick(display);
    flushes = 0;
    for (int i = 0; i < 20; i++) {
        float jitter = i % 2 ? -0.04f : 0.04f;
        if (observed.connected)
            observed.nozzle_temp_cur += jitter;
        if (observed.is_printing)
            observed.progress += jitter;
        tick(display);
    }
    assert(flushes == 0);
}

int main(void)
{
    static uint16_t draw_buffer[320 * 40];

    lv_init();
    deneb_fonts_init();
    lv_display_t *display = lv_display_create(320, 240);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_frame);
    screen_mgr_init();
    screen_mgr_push(&screen_status);
    assert_label(state_label, "status.idle");
    assert_label(file_label, "status.no_file");
    assert_label(progress_label, "0%");
    assert(lv_obj_has_state(pause_resume_btn, LV_STATE_DISABLED));
    assert(lv_obj_has_state(stop_btn, LV_STATE_DISABLED));
    tick(display);
    assert_label(state_label, "status.preparing");
    assert_label(nozzle_temp_label, "-- / --");
    assert_steady(display);

    observed.connected = true;
    observed.nozzle_temp_cur = 21.2f;
    tick(display);
    assert_label(state_label, "status.idle");
    assert_label(nozzle_temp_label, "21 / 0 \u00b0C");
    observed.nozzle_temp_cur = 21.4f;
    assert_steady(display);

    observed.is_printing = true;
    observed.nozzle_temp_cur = 197.2f;
    observed.nozzle_temp_set = 205.0f;
    observed.bed_temp_cur = 61.2f;
    observed.bed_temp_set = 65.0f;
    observed.progress = 42.1f;
    observed.time_left = 3666;
    strcpy(observed.filename, "calibration.ufp");
    tick(display);
    assert_label(state_label, "status.printing");
    assert_label(file_label, "calibration.ufp");
    assert_label(time_left_label, "Left 1:01:06");
    assert_label(progress_label, "42%");
    assert(lv_bar_get_value(progress_bar) == 42);
    assert(!lv_obj_has_state(pause_resume_btn, LV_STATE_DISABLED));
    assert(!lv_obj_has_state(stop_btn, LV_STATE_DISABLED));
    observed.progress = 42.9f;
    observed.nozzle_temp_cur = 197.4f;
    assert_steady(display);

    observed.progress = 43.0f;
    observed.nozzle_temp_cur = 197.6f;
    observed.time_left--;
    tick(display);
    assert_label(nozzle_temp_label, "198 / 205 \u00b0C");
    assert_label(progress_label, "43%");
    assert(lv_bar_get_value(progress_bar) == 43);
    assert_label(time_left_label, "Left 1:01:05");

    lv_obj_set_height(file_label, 14);
    memset(observed.filename, 'W', sizeof(observed.filename) - 1);
    tick(display);
    assert(strstr(lv_label_get_text(file_label), "..."));
    assert_steady(display);

    lv_obj_send_event(pause_resume_btn, LV_EVENT_CLICKED, NULL);
    assert(pauses == 1);
    assert_label(state_label, "print.pausing");
    observed.is_printing = false;
    observed.is_paused = true;
    tick(display);
    assert_label(state_label, "status.paused");
    assert_label(pause_resume_label, "print.resume");
    lv_obj_send_event(pause_resume_btn, LV_EVENT_CLICKED, NULL);
    assert(resumes == 1);
    assert_label(state_label, "print.resuming");
    observed.is_printing = true;
    observed.is_paused = false;
    tick(display);
    lv_obj_send_event(stop_btn, LV_EVENT_CLICKED, NULL);
    assert(stops == 1 && lv_obj_has_state(stop_btn, LV_STATE_DISABLED));
    assert_label(state_label, "print.stopping");
    tick(display);
    assert_label(state_label, "status.cooling");
    observed.has_error = true;
    tick(display);
    assert_label(state_label, "status.error");

    observed = (printer_state_t){0};
    stops = 0;
    translated = true;
    screen_mgr_rebuild_stack();
    assert_label(state_label, "Gereed");
    assert_label(file_label, "Geen bestand");
    tick(display);
    assert_label(state_label, "Verbinden");
    observed.connected = true;
    tick(display);
    assert_label(state_label, "Gereed");
    assert_label(file_label, "Geen bestand");
    assert_steady(display);

    screen_status.destroy();
    lv_display_delete(display);
    lv_deinit();
    return 0;
}
