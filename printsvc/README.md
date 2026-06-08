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
- `motion_send_error.*` owns the native mapping from motion-send return codes
  to Deneb error classes so raw G-code, macro, job stream, abort cleanup, and
  finish cleanup paths do not each duplicate serial-vs-command classification.
- `heater_wait.*` owns native preheat target tracking and readiness checks.
- `job_control.*` owns job acceptance and abort cleanup: active-job rejection,
  stream-open failures, heater-wait setup, and native abort state/motion policy.
  Cleanup policy failures preserve the named motion-send return code before
  mapping to command or serial errors. Idle aborts fail explicitly without
  sending cleanup motion or leaving the abort flag latched.
- `job_streamer.*` owns active-job polling: preheat gating, paused/abort
  checks, bounded line streaming, finish motion policy dispatch, and
  planner-starvation accounting. Stream-send failures close the active stream,
  clear active-job state, and record command or serial lifecycle errors; abort
  requests are finalized through shared job lifecycle cleanup and clear the
  consumed abort latch. Idle polling clears stale abort latches when no active
  job remains. Terminal stream paths clear heater-wait state so preheat status
  cannot reappear after abort, completion, or stream failure.
- `macro_runner.*` streams macro files through callbacks and preserves
  send/poll callback failure codes so `MACRO` command handling can distinguish
  serial transport errors from path/stream failures.
- `pause_resume_control.*` owns command-level pause/resume replies and routes
  accepted requests into the native pause/resume state policy.
- `marlin_packet.*` owns sequence-numbered packet formatting and CRC.
- `crc.*` owns CRC helpers.
- `gcode_stream.*` streams job and macro G-code line by line without loading
  whole files.
- `macro_registry.*` resolves Deneb-owned macro defaults only from
  `DENEB_PRINTSVC_MACRO_DIR` (`/etc/deneb/marlindriver/gcode/`) and fails
  closed when a macro is missing instead of falling back to stock
  `/home/cygnus/marlindriver/gcode/` files.
- `macro_control.*` owns macro command execution against the service runtime:
  flow-window waiting, abort checks, motion polling, G-code send callbacks,
  and command error mapping.
- `../common/print/print_macros.h` names stock macro files used by
  printsvc, touchscreen UI, and web/API callers.
- `../common/print/pending_job_file.*` owns pending-job file parsing,
  conflict-state detection, handled-state updates, display-name fallback, and
  shared pending metadata cleanup for native pending-job metadata, touchscreen
  UI, and web/API callers.
- `../common/print/print_backend_route.*` owns the native-printsvc endpoint
  selection used by touchscreen UI and web/API clients, plus the
  route-diagnostics JSON fields that show which backend and endpoint URLs a
  native client selected.
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

The binary is packaged into Deneb update releases and is now the Deneb print
backend. `deneb-printsvc.init` stops the stock `printserver` before starting
the native service, and Deneb's patched stock `printserver` init shim no longer
launches the old driver or delegates back through a Deneb config flag. Both
handoff paths clear stale `/var/run/printserver.pid` state and terminate an
exact `/home/cygnus/marlindriver/print_service.py` process if it survived the
stock init stop path.
Generated Deneb init shims do not spell stock Python process entry points
directly, and the installer does not patch stock coordinator Python modules for
the native route; Deneb-owned init scripts and C route helpers own the
migration boundary.

Normal device startup fails closed if `/dev/ttyS1` cannot be opened. The
`--dry-run` option is reserved for host/lab debugging and must not be used by
the packaged init script.

This scaffold is not complete enough to run unattended prints. It exists to
make the de-python work buildable and testable while the remaining planner and
firmware buffer semantics, firmware verification, richer finish/abort motion
policy, and live-device validation are implemented.

Abort handling is deliberately owned by `service.*`; the current native path
clears print state without issuing duplicate homing or unsafe XY cleanup moves.
Shared Deneb print-control helpers stay split by concern: status/context rules
live in `common/print/print_state_rules.*`, REST/Cura action parsing and action
plans live in `common/print/print_action_rules.*`, and reusable ASCII matching
helpers live in `common/print/print_string.*`. Print elapsed-time, progress,
and timing normalization math lives in `common/print/print_timing_rules.*`.

## Host Build

```sh
cmake -S printsvc -B /tmp/deneb-printsvc-host -DBUILD_HOST_STUB=ON
cmake --build /tmp/deneb-printsvc-host
ctest --test-dir /tmp/deneb-printsvc-host --output-on-failure
```

CTest also runs the shell smoke verifier selftest and native binary CLI
selftest when `sh` is available, so local host tests cover the native C service
tests, the shell evidence contract used by live Section 8 smoke runs, and the
real `deneb-printsvc --smoke-test` / `--local-job-smoke` entry points. CTest also runs
`deneb-printsvc-init-selftest`, which checks native init and installer handoff
scripts, including the generated printserver shim body, for exact stock-driver
cleanup ordering and no Python driver launch path.

The full release build also cross-compiles `deneb-printsvc` and packages
`deneb-printsvc` plus `deneb-printsvc.init` into the `.deneb` artifact:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```

The release builder verifies the produced `.deneb` archive after packaging: it
must contain `deneb-printsvc` plus the shell smoke/CLI/init selftests and must not
contain Python driver artifacts such as `*.py`, `*python*`, or
`print_service.py`. The package builder runs the staged selftests before
creating the archive, then inspects the archive for the same native/no-Python
invariant; the PowerShell release builder extracts the archived copies and runs
the shell-only smoke/init checks again before accepting the release package.
During install, the update script rejects Python driver artifacts in
`/tmp/update` and runs the installed smoke-tool, native binary CLI, and
init-handoff selftests before completing the update.

## Device Smoke Harness

Release packages install `/usr/bin/deneb-printsvc-smoke`, a no-Python lab
harness for gathering the live Section 8 evidence. Running it with no flags is
observe-only and records route/status/process/resource snapshots to
`/tmp/deneb-printsvc-smoke.log`; it also writes compact phase and `/proc`
memory, CPU jiffy, load, and completed-job throughput evidence to
`/tmp/deneb-printsvc-smoke.summary`. Native-route runs restart and assert
`deneb-printsvc` instead of toggling a stock-driver route, and fail if the
route diagnostic does not report `native_only_route:true` or if
`print_service.py` is still running during native validation.
Every snapshot records the selected print-backend route body, the scalar
`/printer/status` status value plus sanitized full body, and `/printer` root body in
the summary so preheat, pause/resume, abort, completion, and restart runs prove
the native route, UI-visible state, and native active/stop-allowed safety flags
instead of only proving HTTP reachability.
The stock/native comparator requires those `/printer` root active/stop flags on
each required active and inactive lifecycle phase, so a single missing
preheat/Cura/completion safety snapshot fails the comparison.
It also verifies the expected `/printer/status` lifecycle value for each phase
while requiring `native_only_route:true`, so preheat must be reported as
printing, pause as paused, and abort/completion as idle in the comparison
evidence.
`--boot-sync` waits for native-only print-backend route and printer-status
readiness before the initial snapshot and records the bounded ready elapsed
time, scalar status value, and native-only status body evidence.

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

Use only under supervision with clear motion axes and a ready power cutoff.
`--local-job` requires `--native`, runs the native
`deneb-printsvc --local-job-smoke` path through the shared IPC frame helper, and
proves native route ownership, local/USB job acceptance with native `pre_print`
active/stop-allowed state, abort, and final native `idle` inactive/stop-disabled
state without routing through the legacy Python driver. `--job` is the
abort-path exercise; `--complete-job` intentionally waits for a short print to
leave the active-job API without issuing an abort.

Verify a captured summary on-device without Python:

```sh
deneb-printsvc-smoke-verify /tmp/deneb-printsvc-smoke.summary
deneb-printsvc-smoke-verify --native --heat --motion --macro --local-job --job --preheat-abort --cura-job --pause-resume /tmp/native-printsvc.summary
deneb-printsvc-smoke-verify --native --complete-job --restart --resources --boot-sync /tmp/native-printsvc.summary
deneb-printsvc-smoke-compare /tmp/stock-printsvc.summary /tmp/native-printsvc.summary
deneb-printsvc-smoke-selftest
deneb-printsvc-cli-selftest /usr/bin/deneb-printsvc
deneb-printsvc-init-selftest
```

For job runs the verifier requires `printing` while active, `paused` after a
pause command, and `idle` after abort or natural completion, including the
active `printing` snapshot before a completion run is allowed to settle to
idle. It also checks native active/stop-allowed flags during active, preheat,
paused, and resumed snapshots, then requires those flags to be false after
abort or completion.
Heat, motion, and macro verification require their snapshot status and printer
root records so those phases prove backend state sampling, not only command
acceptance.
`--resources` requires initial/final memory, uptime, CPU, load, process RSS
samples, and a bytes/elapsed/bytes-per-second throughput record.
`--boot-sync` requires a successful native-only route/status readiness record
with elapsed and uptime-delta seconds, a scalar status value, and native-only
status-body evidence.
`--native` also requires native-only route body evidence, native-only route
evidence in captured status bodies, native `deneb-printsvc` process evidence,
and no captured native process sample with stock `print_service.py`.
The compare tool reports before/after deltas for memory, process RSS, CPU
jiffies, CPU jiffies consumed between initial/final samples, boot-sync elapsed
time, and print throughput. It fails if the stock summary lacks
initial/final `print_service.py` process evidence, if CPU or throughput
intervals are not positive, or if the native summary lacks native-only route
evidence in route diagnostics or any required status lifecycle body, lacks
scalar boot-sync status plus native-only boot-sync status-body evidence,
reports a wrong lifecycle status value, contains a stock `print_service.py` process
sample, lacks native local/USB IPC job acceptance and stop-state evidence, lacks `deneb-printsvc`
process ownership, or lacks per-lifecycle native stop-safety flags.
The selftest is synthetic and does not replace live hardware evidence; it
builds stock/native summary fixtures and runs the full verifier plus comparator
so the shell evidence gates can be tested without Python. It also runs the
live smoke harness' `--summary-parser-selftest` mode to cover scalar status
extraction without touching hardware. It also checks that
missing native stop-safety evidence, missing status-body native-route evidence
in both verifier and comparator paths, missing native-route evidence in a
single comparator lifecycle status snapshot, a wrong single-phase lifecycle
status value, boot-sync summaries that put the full status response into
`status=` or omit `status_body` native-route proof, missing natural-completion
active status evidence, missing native local/USB job evidence, a non-native-only route diagnostic, a returned stock
`print_service.py` process in a native run,
missing stock `print_service.py` baseline evidence, and zero-throughput records
are rejected.
The CLI selftest runs the actual `deneb-printsvc` binary entry points without
opening the motion serial device, proving `--smoke-test` and the native
`--local-job-smoke` accepted/aborted stop-state rows.
The init selftest checks `deneb-printsvc.init`, installer source, the
installer-generated printserver heredoc, and generated printserver shims for
exact stock `print_service.py` cleanup, stale PID cleanup, native ownership
markers, cleanup ordering, and absence of Python driver launch commands.
