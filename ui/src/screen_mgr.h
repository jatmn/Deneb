/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Screen manager for Deneb UI.
 * Manages a navigation stack of screens (push/pop/replace).
 * Each screen has create/destroy callbacks and optional back behavior.
 */

#ifndef SCREEN_MGR_H
#define SCREEN_MGR_H

#include "lvgl.h"

#define SCREEN_STACK_MAX 8

typedef struct screen_ops {
    const char *name;
    lv_obj_t *(*create)(void);      /* Create and return the screen object */
    void (*destroy)(void);          /* Cleanup when screen is popped */
    bool show_back;                 /* Show back button in header */
} screen_ops_t;

/**
 * Initialize the screen manager.
 */
void screen_mgr_init(void);

/**
 * Push a new screen onto the navigation stack.
 * Previous screen is hidden but preserved.
 * @param ops  Screen operations (must have valid create callback)
 */
void screen_mgr_push(const screen_ops_t *ops);

/**
 * Pop the current screen and return to the previous one.
 * Calls destroy on the popped screen.
 * No-op if stack has only one entry.
 */
void screen_mgr_pop(void);

/**
 * Replace the current screen (pop + push in one step).
 * @param ops  New screen operations
 */
void screen_mgr_replace(const screen_ops_t *ops);

/**
 * Get the current screen's name (for debugging).
 */
const char *screen_mgr_current_name(void);

/**
 * Get current stack depth.
 */
int screen_mgr_depth(void);

#endif /* SCREEN_MGR_H */
