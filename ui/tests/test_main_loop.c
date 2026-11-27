/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include <assert.h>

/* Exercise the production loop, including conflict handling, with a fake clock. */
#undef BACKEND_COMM_STUB
#define clock_gettime test_clock_gettime
#define usleep test_usleep
#define main deneb_main
#include "../src/main.c"
#undef main
#undef usleep
#undef clock_gettime

static uint64_t start_ms;
static uint64_t now_ms;
static uint64_t stop_ms;
static uint32_t lvgl_delay_ms;
static uint32_t first_poll_cost_ms;
static unsigned status_polls;
static uint64_t last_status_ms;
static unsigned conflict_checks;
static unsigned conflict_prompts;
static uint64_t prompt_time_ms;
static bool prompt_evaluated;
static unsigned sleeps;
static unsigned iterations;

const screen_ops_t screen_print_conflict = {0};

int test_clock_gettime(clockid_t id, struct timespec *ts)
{
    assert(id == CLOCK_MONOTONIC);
    ts->tv_sec = (time_t)(now_ms / 1000);
    ts->tv_nsec = (long)(now_ms % 1000) * 1000000;
    return 0;
}

int test_usleep(useconds_t usec)
{
    assert(usec > 0 && usec % 1000 == 0);
    sleeps++;
    now_ms += usec / 1000;
    assert(now_ms <= stop_ms);
    if (now_ms == stop_ms)
        signal_handler(SIGTERM);
    return 0;
}

uint32_t lv_timer_handler(void)
{
    assert(++iterations < 50);
    if (conflict_prompts && !prompt_evaluated) {
        assert(now_ms == prompt_time_ms);
        prompt_evaluated = true;
        return 3;
    }
    if (first_poll_cost_ms > 50 && status_polls == 1) {
        assert(now_ms - start_ms == first_poll_cost_ms);
        assert(sleeps == 0);
    }
    return lvgl_delay_ms;
}

void backend_poll(void)
{
    assert(status_polls == 0 || now_ms - last_status_ms >= 50);
    last_status_ms = now_ms;
    if (status_polls++ == 0)
        now_ms += first_poll_cost_ms;
}

const char *screen_mgr_current_name(void)
{
    return conflict_prompts ? "print_conflict.title" : "status.title";
}

int print_conflict_has_pending(void)
{
    if (conflict_checks++ == 0) {
        assert(now_ms - start_ms == first_poll_cost_ms);
        return 0;
    }
    assert(now_ms - start_ms == first_poll_cost_ms + 1000);
    return 1;
}

void screen_mgr_push(const screen_ops_t *ops)
{
    assert(ops == &screen_print_conflict && conflict_prompts == 0);
    conflict_prompts++;
    prompt_time_ms = now_ms;
}

static void reset_fixture(uint64_t start, uint32_t duration,
                          uint32_t delay, uint32_t poll_cost)
{
    running = 1;
    start_ms = now_ms = start;
    stop_ms = start + duration;
    lvgl_delay_ms = delay;
    first_poll_cost_ms = poll_cost;
    status_polls = conflict_checks = conflict_prompts = sleeps = iterations = 0;
    prompt_evaluated = false;
}

int main(void)
{
    /* Six 8 ms waits before the backend deadline shortens the next wait. */
    reset_fixture(UINT32_MAX - 4, 50, 8, 0);
    run_main_loop();
    assert(sleeps == 7 && status_polls == 1 && conflict_checks == 1);

    /* A 5 ms poll leaves 3 ms until the 8 ms LVGL deadline. */
    reset_fixture(2000, 8, 8, 5);
    run_main_loop();
    assert(sleeps == 1 && status_polls == 1 && conflict_checks == 1);

    /* A slow poll passes both the LVGL and backend deadlines. */
    reset_fixture(2000, 81, 8, 73);
    run_main_loop();
    assert(sleeps == 1 && status_polls == 2 && conflict_checks == 1);

    /* Without a ready LVGL timer, the conflict deadline can beat the backend. */
    reset_fixture(4000, 1008, LV_NO_TIMER_READY, 5);
    run_main_loop();
    assert(status_polls == 21 && conflict_checks == 2 && conflict_prompts == 1);
    assert(prompt_evaluated);
    return 0;
}
