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
- `command.*`, `command_dispatch.*`, and `command_audit.*` keep well-formed
  unknown verbs on the audited command path while malformed frames remain bad
  command input.
- `ipc_frame.*` owns command-frame handling shared by the ZMQ loop and host
  tests.
- `ipc_zmq.*` owns the first-stage ZMQ PUB/REP compatibility boundary:
  status PUB on `127.0.0.1:5555`, command REP on `127.0.0.1:5556`, and topic
  `10001`. Command-frame handling is split into a host-tested helper so valid
  commands, audited unknown verbs, and malformed-frame replies stay consistent.
- `status.*` serializes stock-shaped status JSON.
- `status_parser.*` parses Marlin temperature, position, version, and fault
  lines.
- `serial_transport.*` opens and configures the Marlin serial device.
- `motion_runtime.*` owns serial runtime state: Marlin transport open/close,
  readiness, line polling, observation dispatch, and resend handoff; resend
  write failures are reported as serial errors when the transport is marked
  ready.
- `flow_control.*` tracks sequence numbers, in-flight packets, ACKs, rejects,
  and resend requests.
- `motion_sender.*` returns named negative failure codes for invalid commands,
  flow-window pressure, and serial transport writes so command handlers can map
  failures to the correct native error class.
- `heater_wait.*` owns native preheat target tracking and readiness checks.
- `job_control.*` owns job acceptance and abort cleanup: active-job rejection,
  stream-open failures, heater-wait setup, and native abort state/motion policy.
- `job_streamer.*` owns active-job polling: preheat gating, paused/abort
  checks, bounded line streaming, finish motion policy dispatch, and
  planner-starvation accounting. Stream-send failures close the active stream,
  clear active-job state, and record command or serial lifecycle errors; abort
  requests are finalized through shared job lifecycle cleanup.
- `macro_runner.*` streams macro files through callbacks and preserves
  send/poll callback failure codes so `MACRO` command handling can distinguish
  serial transport errors from path/stream failures.
- `pause_resume_control.*` owns command-level pause/resume replies and routes
  accepted requests into the native pause/resume state policy.
- `marlin_packet.*` owns sequence-numbered packet formatting and CRC.
- `crc.*` owns CRC helpers.
- `gcode_stream.*` streams job and macro G-code line by line without loading
  whole files.
- `macro_registry.*` resolves Deneb-owned macro defaults from
  `DENEB_PRINTSVC_MACRO_DIR` (`/etc/deneb/marlindriver/gcode/`), with
  `DENEB_PRINTSVC_STOCK_MACRO_RECOVERY_DIR` retained only as an explicit
  recovery fallback. Deneb macro resolution does not require the stock
  directory to exist.
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

When the native service starts, it appends to
`/var/log/ultimaker/deneb-printsvc.log` and falls back to `/tmp` if that path
is unavailable. Status lines intentionally keep stock-shaped keys beside native
fields: `stock.req`, `stock.file`, heater temperatures, position, and fault
state are logged with `native.phase`, `native.stopAllowed`, serial
ACK/reject/resend counters, queue depth, streamed job line, command latency,
and planner-starvation count. Command lines record the accepted verb, target
file or macro, result, and latency.

## Safety State

The binary is packaged into Deneb update releases and is now the default Deneb
print backend when no explicit fallback flag exists. Missing
`deneb.printsvc.enabled` values are treated the same as `1`, the installer sets
`deneb.printsvc.enabled=1` on fresh installs, `deneb-printsvc.init` stops the
stock `printserver` before starting the native service, and Deneb's patched
stock `printserver` init shim no longer launches the old driver from Deneb
code. Setting the flag back to `0` delegates to the backed-up stock init script
as an explicit recovery path without a reflash. Generated Deneb init shims do
not spell stock Python process entry points directly, and the installer does
not patch stock coordinator Python modules for the native route; Deneb-owned
init scripts and C route helpers own the migration boundary.

Normal device startup fails closed if `/dev/ttyS1` cannot be opened. The
`--dry-run` option is reserved for host/lab debugging and must not be used by
the packaged init script.

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

## Device Smoke Harness

Release packages install `/usr/bin/deneb-printsvc-smoke`, a no-Python lab
harness for gathering the live Section 8 evidence. Running it with no flags is
observe-only and records route/status/process/resource snapshots to
`/tmp/deneb-printsvc-smoke.log`; it also writes compact phase and `/proc`
memory, CPU jiffy, load, and completed-job throughput evidence to
`/tmp/deneb-printsvc-smoke.summary`. Native-route runs restore the previous
route by default and record a post-restore snapshot.
Every snapshot records the scalar `/printer/status` body in the summary so
preheat, pause/resume, abort, completion, and restart runs prove the UI-visible
state instead of only proving HTTP reachability.
`--boot-sync` waits for print-backend route and printer-status readiness before
the initial snapshot and records the bounded ready elapsed time.

Risky phases are explicit:

```sh
deneb-printsvc-smoke --native
deneb-printsvc-smoke --boot-sync
deneb-printsvc-smoke --native --heat
deneb-printsvc-smoke --native --motion
deneb-printsvc-smoke --native --macro home
deneb-printsvc-smoke --native --local-job /mnt/usb/local-test.gcode
deneb-printsvc-smoke --native --job /home/3D/test.gcode --pause-resume
deneb-printsvc-smoke --native --preheat-abort /home/3D/preheat-test.gcode
deneb-printsvc-smoke --native --cura-job /home/3D/cura-test.gcode
deneb-printsvc-smoke --native --complete-job /home/3D/short-test.gcode
deneb-printsvc-smoke --native --restart
deneb-printsvc-smoke --native --summary /tmp/native-printsvc.summary
```

The script restores the previous `deneb.printsvc.enabled` value by default.
Use only under supervision with clear motion axes and a ready power cutoff.
`--local-job` runs the native `deneb-printsvc --local-job-smoke` path through
the shared IPC frame helper and proves local/USB job acceptance, active
`printing` status, abort, and final `idle` status without routing through the
legacy Python driver. `--job` is the
abort-path exercise; `--complete-job` intentionally waits for a short print to
leave the active-job API without issuing an abort.

Verify a captured summary on-device without Python:

```sh
deneb-printsvc-smoke-verify /tmp/deneb-printsvc-smoke.summary
deneb-printsvc-smoke-verify --native --heat --motion --macro --local-job --job --preheat-abort --cura-job --pause-resume /tmp/native-printsvc.summary
deneb-printsvc-smoke-verify --native --complete-job --restart --resources --boot-sync /tmp/native-printsvc.summary
deneb-printsvc-smoke-compare /tmp/stock-printsvc.summary /tmp/native-printsvc.summary
```

For job runs the verifier requires `printing` while active, `paused` after a
pause command, and `idle` after abort or natural completion. `--resources`
requires initial/final memory, uptime, CPU, load, and process RSS samples, and
`--complete-job` requires a bytes/elapsed/bytes-per-second throughput record.
`--boot-sync` requires a successful route/status readiness record with elapsed
and uptime-delta seconds.
The compare tool reports before/after deltas for memory, process RSS, CPU
jiffies, boot-sync elapsed time, and print throughput.
