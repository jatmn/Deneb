/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Deneb Touchscreen UI - main entry point. LVGL v9.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "drivers/fb_driver.h"
#include "drivers/touch_driver.h"
#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"

extern const screen_ops_t screen_home;

static volatile int running = 1;

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

int main(int argc, char *argv[])
{
    const char *lang = "en";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            lang = argv[++i];
        } else if (strcmp(argv[i], "--smoke-test") == 0) {
            fprintf(stderr, "deneb-ui: smoke test ok\n");
            return 0;
        }
    }

    fprintf(stderr, "deneb-ui: starting (lang=%s)\n", lang);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    lv_init();
    locale_init(lang);

    if (fb_driver_init() < 0) {
        fprintf(stderr, "deneb-ui: framebuffer init failed\n");
        return 1;
    }

    if (touch_driver_init() < 0) {
        fprintf(stderr, "deneb-ui: touchscreen init failed (continuing)\n");
    }

    if (backend_init() < 0) {
        fprintf(stderr, "deneb-ui: backend init failed (continuing without IPC)\n");
    }

    screen_mgr_init();
    screen_mgr_push(&screen_home);

    fprintf(stderr, "deneb-ui: entering main loop\n");

    uint32_t last_status_poll = 0;

    while (running) {
        uint32_t now = custom_tick_get();

        lv_tick_inc(5);
        lv_timer_handler();

        /* Poll backend status every 50ms (20Hz) */
        if (now - last_status_poll >= 50) {
            backend_poll();
            last_status_poll = now;
        }

        usleep(5000); /* 5ms */
    }

    fprintf(stderr, "deneb-ui: shutting down\n");
    backend_deinit();
    locale_deinit();
    touch_driver_deinit();
    fb_driver_deinit();
    lv_deinit();

    return 0;
}
