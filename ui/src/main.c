/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Deneb Touchscreen UI - main entry point. LVGL v9.
 */

#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if !defined(_WIN32)
#include <syslog.h>
#include <sys/stat.h>
#endif

#include "lvgl.h"
#include "drivers/fb_driver.h"
#include "drivers/touch_driver.h"
#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"

extern const screen_ops_t screen_home;
extern const screen_ops_t screen_status;
extern const screen_ops_t screen_print;
extern const screen_ops_t screen_material;
extern const screen_ops_t screen_maintenance;
extern const screen_ops_t screen_jog;
extern const screen_ops_t screen_temp;
extern const screen_ops_t screen_settings;
extern const screen_ops_t screen_update;
extern const screen_ops_t screen_level;
extern const screen_ops_t screen_diagnostics;
extern const screen_ops_t screen_language;
extern const screen_ops_t screen_nozzle_size;
extern const screen_ops_t screen_network;
extern const screen_ops_t screen_digital_factory;
extern const screen_ops_t screen_frame_lighting;
extern const screen_ops_t screen_factory_reset;
extern const screen_ops_t screen_about;
extern const screen_ops_t screen_set_material;
extern const screen_ops_t screen_error;
extern const screen_ops_t screen_print_conflict;
void frame_lighting_schedule_saved_apply(void);
void error_screen_show(const char *er_code, const char *description,
                       const char *action);
int print_conflict_has_pending(void);
#ifdef BACKEND_COMM_STUB
void screen_network_set_catalog_placeholder_mode(int enabled);
#endif

static volatile int running = 1;

static const char *program_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

static uint32_t custom_tick_get(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#ifdef BACKEND_COMM_STUB
typedef struct screenshot_screen {
    const char *slug;
    const screen_ops_t *ops;
    const char *er_code;
    const char *er_desc;
    const char *er_action;
} screenshot_screen_t;

static void render_for_screenshot(void)
{
    lv_obj_invalidate(lv_screen_active());
    for (int i = 0; i < 8; i++) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
    lv_refr_now(NULL);
}

static int save_current_screenshot(const char *dir, const char *slug)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, slug);
    render_for_screenshot();
    return fb_driver_save_ppm(path);
}

static int render_screenshot_entry(const char *dir,
                                   const screenshot_screen_t *screen)
{
    int rc = 0;

    screen_mgr_push(screen->ops);
    if (screen->er_code) {
        error_screen_show(screen->er_code, screen->er_desc,
                          screen->er_action);
    }

    if (save_current_screenshot(dir, screen->slug) < 0) {
        fprintf(stderr, "deneb-ui: failed to save %s screenshot\n",
                screen->slug);
        rc = 1;
    }
    screen_mgr_pop();
    return rc;
}

static int run_screenshot_catalog(const char *dir, const char *only_slug)
{
#if !defined(_WIN32)
    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
        perror("deneb-ui: mkdir screenshot dir");
        return 1;
    }
#endif

    screen_network_set_catalog_placeholder_mode(1);

    const screenshot_screen_t screens[] = {
        {"status", &screen_status, NULL, NULL, NULL},
        {"print-from-usb", &screen_print, NULL, NULL, NULL},
        {"print-conflict", &screen_print_conflict, NULL, NULL, NULL},
        {"material", &screen_material, NULL, NULL, NULL},
        {"set-material", &screen_set_material, NULL, NULL, NULL},
        {"maintenance", &screen_maintenance, NULL, NULL, NULL},
        {"temperature", &screen_temp, NULL, NULL, NULL},
        {"update-firmware", &screen_update, NULL, NULL, NULL},
        {"manual-control", &screen_jog, NULL, NULL, NULL},
        {"level-build-plate", &screen_level, NULL, NULL, NULL},
        {"diagnostics", &screen_diagnostics, NULL, NULL, NULL},
        {"settings", &screen_settings, NULL, NULL, NULL},
        {"language", &screen_language, NULL, NULL, NULL},
        {"nozzle-size", &screen_nozzle_size, NULL, NULL, NULL},
        {"network", &screen_network, NULL, NULL, NULL},
        {"digital-factory", &screen_digital_factory, NULL, NULL, NULL},
        {"frame-lighting", &screen_frame_lighting, NULL, NULL, NULL},
        {"factory-reset", &screen_factory_reset, NULL, NULL, NULL},
        {"about", &screen_about, NULL, NULL, NULL},
        {"error", &screen_error, "ER999",
         "Example blocking printer error shown for catalog reference.",
         "Resolve the condition, then confirm on the printer."},
    };

    if (only_slug && strcmp(only_slug, "all") == 0)
        only_slug = NULL;

    if (!only_slug || strcmp(only_slug, "home") == 0) {
        if (save_current_screenshot(dir, "home") < 0) {
            fprintf(stderr, "deneb-ui: failed to save home screenshot\n");
            screen_network_set_catalog_placeholder_mode(0);
            return 1;
        }
        if (only_slug) {
            screen_network_set_catalog_placeholder_mode(0);
            return 0;
        }
    }

    for (size_t i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) {
        if (only_slug && strcmp(only_slug, screens[i].slug) != 0)
            continue;
        if (render_screenshot_entry(dir, &screens[i]) != 0) {
            screen_network_set_catalog_placeholder_mode(0);
            return 1;
        }
        if (only_slug) {
            screen_network_set_catalog_placeholder_mode(0);
            return 0;
        }
    }

    screen_network_set_catalog_placeholder_mode(0);

    if (only_slug) {
        fprintf(stderr, "deneb-ui: unknown screenshot screen: %s\n",
                only_slug);
        return 1;
    }

    return 0;
}
#endif

int main(int argc, char *argv[])
{
    const char *lang = "en";
    const char *screenshot_dir = NULL;
    const char *screenshot_screen = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            lang = argv[++i];
#ifdef BACKEND_COMM_STUB
        } else if (strcmp(argv[i], "--screenshot-dir") == 0 && i + 1 < argc) {
            screenshot_dir = argv[++i];
        } else if (strcmp(argv[i], "--screenshot-screen") == 0 && i + 1 < argc) {
            screenshot_screen = argv[++i];
#endif
        } else if (strcmp(argv[i], "--smoke-test") == 0) {
            fprintf(stderr, "deneb-ui: smoke test ok\n");
            return 0;
        }
    }

#if !defined(_WIN32)
    openlog("deneb-ui", LOG_PID, LOG_DAEMON);
#endif

    fprintf(stderr, "deneb-ui: starting (lang=%s)\n", lang);
#if !defined(_WIN32)
    syslog(LOG_INFO, "starting lang=%s version=%s", lang, DENEB_VERSION);
#endif

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    lv_init();
    deneb_fonts_init();
    locale_init(lang);

    if (fb_driver_init() < 0) {
        fprintf(stderr, "deneb-ui: framebuffer init failed\n");
#if !defined(_WIN32)
        syslog(LOG_ERR, "framebuffer init failed");
        closelog();
#endif
        return 1;
    }

    if (touch_driver_init() < 0) {
        fprintf(stderr, "deneb-ui: touchscreen init failed (continuing)\n");
#if !defined(_WIN32)
        syslog(LOG_WARNING, "touchscreen init failed; continuing");
#endif
    }

    if (backend_init() < 0) {
        fprintf(stderr, "deneb-ui: backend init failed (continuing without IPC)\n");
#if !defined(_WIN32)
        syslog(LOG_WARNING, "backend init failed; continuing without IPC");
#endif
    }

    screen_mgr_init();
    screen_mgr_push(&screen_home);
    if (!screenshot_dir)
        screen_mgr_push(&screen_status);

#ifndef BACKEND_COMM_STUB
    frame_lighting_schedule_saved_apply();
#endif

#ifdef BACKEND_COMM_STUB
    if (screenshot_dir) {
        int rc = run_screenshot_catalog(screenshot_dir, screenshot_screen);
        backend_deinit();
        locale_deinit();
        touch_driver_deinit();
        fb_driver_deinit();
        lv_deinit();
#if !defined(_WIN32)
        closelog();
#endif
        return rc;
    }
#endif

    fprintf(stderr, "deneb-ui: entering main loop\n");
#if !defined(_WIN32)
    syslog(LOG_INFO, "entered main loop");
#endif

    uint32_t last_status_poll = 0;
    uint32_t last_conflict_poll = 0;

    while (running) {
        uint32_t now = custom_tick_get();

        lv_tick_inc(5);
        lv_timer_handler();

        /* Poll backend status every 50ms (20Hz) */
        if (now - last_status_poll >= 50) {
            backend_poll();
            last_status_poll = now;
        }

#ifndef BACKEND_COMM_STUB
        if (now - last_conflict_poll >= 1000) {
            const char *current = screen_mgr_current_name();
            if (strcmp(current, "print_conflict.title") != 0 &&
                print_conflict_has_pending()) {
                screen_mgr_push(&screen_print_conflict);
            }
            last_conflict_poll = now;
        }
#endif

        usleep(5000); /* 5ms */
    }

    fprintf(stderr, "deneb-ui: shutting down\n");
#if !defined(_WIN32)
    syslog(LOG_INFO, "shutting down");
#endif
    backend_deinit();
    locale_deinit();
    touch_driver_deinit();
    fb_driver_deinit();
    lv_deinit();

#if !defined(_WIN32)
    closelog();
#endif
    return 0;
}
