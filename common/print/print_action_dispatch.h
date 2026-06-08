/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PRINT_ACTION_DISPATCH_H
#define DENEB_COMMON_PRINT_ACTION_DISPATCH_H

#include "print_state_rules.h"

typedef struct {
    void *ctx;
    int (*send_pause)(void *ctx);
    int (*send_resume)(void *ctx);
    int (*send_abort)(void *ctx);
    int (*send_stop)(void *ctx);
    void (*clear_pending)(void *ctx);
} deneb_print_action_dispatch_ops_t;

int deneb_print_action_dispatch(
    const deneb_print_action_plan_t *plan,
    const deneb_print_action_dispatch_ops_t *ops);

#endif
