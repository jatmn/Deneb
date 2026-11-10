/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_BACKGROUND_ACTION_H
#define DENEB_BACKGROUND_ACTION_H
#include <sys/types.h>
/* The child holds the advisory lock across exec, including UI restarts.
 * Caller polls/reaps its child; a nonzero shell exit never means completion. */
int deneb_background_action_start(const char *lock_path, const char *command,
                                   const char *log_path, pid_t *pid);
int deneb_background_action_running(const char *lock_path);
/* 0 running, 1 successful exit, -1 failure. */
int deneb_background_action_poll(pid_t *pid);
#endif
