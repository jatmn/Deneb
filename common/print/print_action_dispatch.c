/* SPDX-License-Identifier: MPL-2.0 */
#include "print_action_dispatch.h"

int deneb_print_action_dispatch(
    const deneb_print_action_plan_t *plan,
    const deneb_print_action_dispatch_ops_t *ops)
{
    int rc = -1;

    if (!plan || !ops)
        return -1;

    switch (plan->kind) {
        case DENEB_PRINT_ACTION_PLAN_PAUSE:
            if (ops->send_pause)
                rc = ops->send_pause(ops->ctx);
            break;
        case DENEB_PRINT_ACTION_PLAN_RESUME:
            if (ops->send_resume)
                rc = ops->send_resume(ops->ctx);
            break;
        case DENEB_PRINT_ACTION_PLAN_ABORT:
            if (ops->send_abort)
                rc = ops->send_abort(ops->ctx);
            break;
        case DENEB_PRINT_ACTION_PLAN_STOP:
            if (ops->send_stop)
                rc = ops->send_stop(ops->ctx);
            break;
        case DENEB_PRINT_ACTION_PLAN_CLEAR_PENDING:
            rc = 0;
            break;
        default:
            return -1;
    }

    if (rc == 0 && plan->clear_pending_after_success &&
        ops->clear_pending)
        ops->clear_pending(ops->ctx);

    return rc;
}
