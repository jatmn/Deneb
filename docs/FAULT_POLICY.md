# Deneb Fault Policy

## Faults and transport recovery

A latched native fault or `ERROR` state stops host job/manual submission and
cancels pending preparation, pause, resume and cleanup policies. The service
preserves the fault and job context for diagnosis. A new `JOB` does not clear
it. Normal print parking/homing is never launched as fault cleanup.

Recoverable controller resend/sequence responses remain part of transport flow
control while the service is healthy. Once a fault is latched, subsequent
responses, including responses in the same receive batch, cannot replay queued
movement or automatically resynchronize it.

| Fault | Response |
| --- | --- |
| Thermal or firmware fault | Stop host submission; retain ERROR; operator/controller recovery required |
| Endstop or homing fault | Stop host submission; retain ERROR; investigate before supervised recovery |
| Storage or host workflow timeout | Stop host submission; retain ERROR; repair the cause before restarting work |
| Recoverable serial resend/sequence response | Existing bounded flow recovery while no fault is latched |
| Transport recovery failure | Latch ERROR and stop host submission |
| Invalid RPC or rejected command | Return an error; do not advance client state as successful work |

## Best-effort shutdown

On the first fault poll, obsolete resend slots are retired while preserving the
transport sequence counter. If the serial link is open, the service attempts
`M401`, `M104 S0` and `M140 S0` once. Repeated polls and ABORT do not repeat this
sequence or erase ERROR. Status retains observed heater targets until telemetry
reports otherwise; submitting a shutdown command is not cooling evidence.

This is **best effort**. A broken serial link, partial write or sequence gap can
prevent acceptance. Connect's M401 is queued and cannot interrupt every blocked
controller command. This path does not replace MCU thermal protection or an
operator's physical emergency response. Recovery requires resolving the cause
and restarting the service/controller as appropriate; there is no automatic
fault-clear transition from JOB or ABORT.

## Evidence boundary

Host fixtures cover fault precedence over heat waits, cancellation of host
queues, fault-plus-resend in one receive batch, one-shot shutdown, and retained
ERROR. They do not prove physical stop latency, cooling, or recovery on a
printer. Do not induce destructive hardware faults in automated tests.
