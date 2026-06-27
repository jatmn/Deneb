# Deneb Fault Policy

Date: 2026-06-22

## Purpose

Define which native faults immediately abort an active print, which only
display an error, and which are recoverable. This policy replaces the stock
coordinator `FaultBreadcrumb` auto-abort behavior from
`rootfs/home/cygnus/coordinator/handlers/faulthandling.py`.

## Fault Categories

| Category | Auto-Abort While Printing? | Auto-Abort While Idle? | Recoverable? |
|---|---|---|---|
| **Thermal fault** (thermal runaway, min/max temp, heater failure) | Yes | No (display error) | No; printer must cool and reset |
| **Endstop fault** (homing failure, endstop trigger) | Yes | No (display error) | Yes; re-home and retry |
| **Marlin firmware fault** (`Error:`, `!!`, `halted`) | Yes | No (display error) | No; requires motion controller reset |
| **Serial fault** (line number, checksum, resend errors) | No | No | Yes; automatically retried |
| **Storage fault** (file I/O errors) | Yes | No (display error) | No; file must be re-uploaded |
| **Command fault** (unknown command, bad command) | No | No | Yes; command is skipped |

## Implementation

Fault policy is enforced by the status parser and print-service poll loop:

1. **Status parser** (`status_parser.c`): Sets `deneb_status_t.fault = true`,
   `state = DENEB_PRINT_STATE_ERROR`, and populates `error` with
   `deneb_error_from_marlin_line()` when a Marlin error/fault line is seen.

2. **Poll loop** (`service.c`): After each `deneb_motion_runtime_poll()`,
   checks if `svc->status.state == DENEB_PRINT_STATE_ERROR` during an active
   print. If so, and the fault category requires auto-abort, calls
   `deneb_job_control_abort()`.

3. **Non-fatal faults**: Serial and command faults are detected by the error
   mapper and marked as recoverable. They are logged but do not trigger abort.

## Safety Notes

- Inducing destructive hardware faults (e.g., thermal runaway by disabling a
  heater) must never be done in automated tests. Use simulated status lines or
  parser fixtures instead.
- Controlled non-destructive fault tests (e.g., endstop trigger during manual
  jog) are acceptable with physical safety gates enabled.