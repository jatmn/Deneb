/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_policy.h"

#include <string.h>

static void policy_clear(deneb_motion_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
}

static void policy_add(deneb_motion_policy_t *policy, const char *command)
{
    if (policy->count >= DENEB_PRINTSVC_MAX_COMMANDS)
        return;
    strncpy(policy->commands[policy->count], command,
            sizeof(policy->commands[policy->count]) - 1);
    policy->count++;
}

void deneb_motion_policy_abort(deneb_motion_policy_t *policy)
{
    policy_clear(policy);
    policy_add(policy, "M400");
    policy_add(policy, "M104 S0");
    policy_add(policy, "M140 S0");
    policy_add(policy, "M106 S0");
    policy_add(policy, "M84 E");
}

void deneb_motion_policy_finish(deneb_motion_policy_t *policy)
{
    policy_clear(policy);
    policy_add(policy, "M400");
    policy_add(policy, "M104 S0");
    policy_add(policy, "M140 S0");
    policy_add(policy, "M106 S0");
    policy_add(policy, "M84");
}

int deneb_motion_policy_contains_xy_home(const deneb_motion_policy_t *policy)
{
    if (!policy)
        return 0;
    for (size_t i = 0; i < policy->count; i++) {
        const char *cmd = policy->commands[i];
        if (strncmp(cmd, "G28", 3) == 0 &&
            (strchr(cmd, 'X') || strchr(cmd, 'Y') || strlen(cmd) == 3))
            return 1;
    }
    return 0;
}
