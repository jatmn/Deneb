# Deneb Print Service

`deneb-printsvc` is the native C replacement track for the stock
`/home/cygnus/marlindriver/print_service.py` service.

The service is intentionally split into focused source units from the first
scaffold:

- `command.*` parses the stock `COMMAND<json>` request frames.
- `command_audit.*` wraps command execution with latency clamping,
  diagnostic-counter refresh, and low-volume command/status log emission.
- `command_dispatch.*` routes parsed `GCODE`, `MACRO`, `JOB`, `ABORT`,
  `PAUSE`, and `RESUME` commands into native driver policies and reply
  formatting.
- `gcode_control.*` owns raw `GCODE` command execution, multi-line motion-send
  loops, command error mapping, and stock-compatible replies.
- `common/print/command_format.*` formats stock `GCODE`, `MACRO`, `JOB`, and
  action frames and owns the stock print-service command verbs for the service,
  web backend, and touchscreen backend.
- `common/print/json_field.*` extracts flat stock-status JSON fields shared by
  web and touchscreen backend status consumers.
- `common/print/print_state_rules.*` owns shared active-print, preheat, paused,
  abort, and transient macro-file classification used by web/touch clients.
- `ipc_zmq.*` owns the first-stage ZMQ PUB/REP compatibility boundary:
  status PUB on `127.0.0.1:5555`, command REP on `127.0.0.1:5556`, and topic
  `10001`.
- `status.*` serializes stock-shaped status JSON.
- `status_parser.*` parses Marlin temperature, position, version, and fault
  lines.
- `serial_transport.*` opens and configures the Marlin serial device.
- `motion_runtime.*` owns serial runtime state: Marlin transport open/close,
  readiness, line polling, observation dispatch, and resend handoff.
- `flow_control.*` tracks sequence numbers, in-flight packets, ACKs, rejects,
  and resend requests.
- `heater_wait.*` owns native preheat target tracking and readiness checks.
- `job_control.*` owns job acceptance and abort cleanup: active-job rejection,
  stream-open failures, heater-wait setup, and native abort state/motion policy.
- `job_streamer.*` owns active-job polling: preheat gating, paused/abort
  checks, bounded line streaming, finish motion policy dispatch, and
  planner-starvation accounting.
- `pause_resume_control.*` owns command-level pause/resume replies and routes
  accepted requests into the native pause/resume state policy.
- `marlin_packet.*` owns sequence-numbered packet formatting and CRC.
- `crc.*` owns CRC helpers.
- `gcode_stream.*` streams job and macro G-code line by line without loading
  whole files.
- `macro_registry.*` resolves stock macro names under
  `/home/cygnus/marlindriver/gcode/`.
- `macro_control.*` owns macro command execution against the service runtime:
  flow-window waiting, abort checks, motion polling, G-code send callbacks,
  and command error mapping.
- `../common/print/print_macros.h` names stock macro files used by
  printsvc, touchscreen UI, and web/API callers.
- `../common/print/pending_job_file.*` owns pending-job file parsing,
  conflict-state detection, handled-state updates, display-name fallback, and
  shared pending metadata cleanup for native pending-job metadata, touchscreen
  UI, and web/API callers.
- `../common/print/print_backend_route.*` owns the stock-coordinator versus
  native-printsvc endpoint selection used by touchscreen UI and web/API
  clients, plus the route-diagnostics JSON fields that show which backend and
  endpoint URLs a native client selected.
- `../common/print/print_state_rules.*` owns shared print-state labels, manual
  action gates, temperature-target readiness, elapsed time, progress
  percentage, current-job fallback identity, active/preparing/stoppable context
  decisions, and transient macro-file filtering for native service,
  touchscreen UI, and web/API callers.
- `diagnostics_log.*` writes low-volume comparison lines that pair stock-shaped
  status fields with native counters and latency fields for lab validation.
- `service_command.*` owns service-level command handling: audit wrapping,
  dispatch handoff, and command reply propagation.
- `service_context.*` owns service initialization, lower-level runtime
  adapter wiring, diagnostics projection, and close cleanup.
- `service.*` owns the print lifecycle state machine and command dispatch.

## Diagnostics Log

When the lab-gated service starts, it appends to
`/var/log/ultimaker/deneb-printsvc.log` and falls back to `/tmp` if that path
is unavailable. Status lines intentionally keep stock-shaped keys beside native
fields: `stock.req`, `stock.file`, heater temperatures, position, and fault
state are logged with `native.phase`, `native.stopAllowed`, serial
ACK/reject/resend counters, queue depth, streamed job line, command latency,
and planner-starvation count. Command lines record the accepted verb, target
file or macro, result, and latency.

## Safety State

The binary is packaged into Deneb update releases, but the init script is
lab-gated by `deneb.printsvc.enabled=0` by default. Installing Deneb therefore
does not disable or replace stock `printserver` yet. When lab testing sets
`deneb.printsvc.enabled=1`, `deneb-printsvc.init` stops the stock `printserver`
before starting the native service, and Deneb's patched stock `printserver`
init script skips `print_service.py` while that flag remains enabled. Setting
the flag back to `0` restores the stock print-service startup path without a
reflash.

This scaffold is not complete enough to run unattended prints. It exists to
make the de-python work buildable and testable while the remaining planner and
firmware buffer semantics, firmware verification, richer finish/abort motion
policy, and live-device validation are implemented.

Abort handling is deliberately owned by `service.*`; the current native path
clears print state without issuing duplicate homing or unsafe XY cleanup moves.

## Host Build

```sh
cmake -S printsvc -B /tmp/deneb-printsvc-host -DBUILD_HOST_STUB=ON
cmake --build /tmp/deneb-printsvc-host
ctest --test-dir /tmp/deneb-printsvc-host --output-on-failure
```

The full release build also cross-compiles `deneb-printsvc` and packages
`deneb-printsvc` plus `deneb-printsvc.init` into the `.deneb` artifact:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```
