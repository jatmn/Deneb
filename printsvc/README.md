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
- `status_parser.*` parses Marlin hotend/bed/topcap temperature, position/R0,
  home-distance, and fault lines, and recognizes version-output lines. It
  includes both plain `T:` and old indexed `T0:` nozzle temperature reports, old
  `B25.1/0.0@0` bed reports, and old `t1/33.2` topcap reports from `M105`. It
  recognizes stock firmware/version output by parsing old `MACHINE_TYPE`,
  `PCB_ID`, and quoted `BUILD` output plus generic `FIRMWARE_NAME` output, then
  serializing `firmware`, `machineType`, `pcbId`, and `pcbIdValid` when those
  lines have actually been observed.
- `serial_transport.*` opens and configures the Marlin serial device.
- `motion_runtime.*` owns serial runtime state: Marlin transport open/close,
  readiness, line polling, observation dispatch, and resend handoff; resend
  write failures and repeated protocol rejects are reported as serial errors
  when the transport is marked ready.
- `flow_control.*` tracks sequence numbers, in-flight packets, ACKs, rejects,
  and resend requests. It preserves recently acknowledged packet history for
  late firmware resends, parses compact CRC ACK/reject sync packets, and
  resyncs stale unreplayable resend requests. Repeated replayable resend
  requests are bounded so protocol storms fail visibly instead of leaving the
  native print service stuck in `printing`.
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
  consumed abort latch. End-of-file dispatches finish cleanup and leaves a
  finish-cleanup pending marker so status stays active until the finish policy
  drains through flow control. Idle polling clears stale abort latches when no
  active job remains. Terminal stream paths clear heater-wait state so preheat
  status cannot reappear after abort, completion, or stream failure.
- `macro_runner.*` streams macro files through callbacks and preserves
  send/poll callback failure codes so `MACRO` command handling can distinguish
  serial transport errors from path/stream failures.
- `pause_resume_control.*` owns command-level pause/resume replies, saved
  X/Y/Z/E/R0/nozzle pause state, stock-derived retract/park/cool and
  reheat/restore policies, and the streaming gate that keeps queued print lines
  paused until resume cleanup drains.
- `marlin_packet.*` owns sequence-numbered packet formatting and CRC.
- `crc.*` owns CRC helpers.
- `gcode_stream.*` streams job and macro G-code line by line without loading
  whole files.
- `gcode_rewrite.*` owns stock-derived line normalization: display-only `M117`
  skip, `M109`/`M190` conversion to service-managed `M104`/`M140` waits, and
  `G280` prime expansion for streamed jobs/macros. Job and macro execution
  honor the generated heater-wait markers in service code.
- `gcode_control.*` owns raw `GCODE` queueing and drains rewritten multi-line
  command batches without sending commands after `M109`/`M190` until the
  corresponding service-side heater wait has cleared.
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
- `service.*` owns the print lifecycle state machine, command dispatch,
  startup M115/M105/M114 probing after serial open, bounded M115 retry while
  firmware metadata is absent, and recurring M105/M114 status polling.

Active pause follows the stock callback ordering for restore coordinates:
pause control sends a standalone `M114`, waits for that packet to drain, then
snapshots X/Y/Z/E/R0/nozzle setpoint before starting the retract/park/cool
policy. This avoids resuming from a stale periodic position sample.

Completion follows the stock service callback cleanup boundary: the native
service keeps the job active until finish cleanup and drain evidence completes,
then marks the lifecycle complete and clears active file/source/UUID/heater
targets so downstream clients do not keep a stale completed job context.

Latched aborts are consumed at the service lifecycle boundary before job
streaming, so internal abort requests use the same cleanup-before-idle path as
explicit `ABORT` commands instead of bypassing cleanup inside the streamer.

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

CTest also runs the shell smoke verifier selftest, native binary CLI selftest,
static native-route audit, and native-audit negative-fixture selftest when `sh`
is available, so local host tests cover the native C service tests, the shell
evidence contract used by live Section 8 smoke runs, the real
`deneb-printsvc --smoke-test` / `--local-job-smoke` entry points, source-level
checks that Deneb clients still route directly to the native print service, and
failing fixtures for stock route selectors, stock Python launchers, missing
package audit wiring, missing installer audit-selftest wiring, missing audit
artifacts, Python driver artifacts, and missing manifest evidence gates. CTest
also runs `deneb-printsvc-init-selftest`,
which checks native init and installer handoff scripts, including the generated
printserver shim body, for exact stock-driver cleanup ordering and no Python
driver launch path.

The full release build also cross-compiles `deneb-printsvc` and packages
`deneb-printsvc` plus `deneb-printsvc.init` into the `.deneb` artifact:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```

The release builder verifies the produced `.deneb` archive after packaging: it
must contain `deneb-printsvc` plus the shell smoke/CLI/init/native-audit
selftests, the guarded stock-baseline collector, the release-gate selftest,
`deneb-printsvc-native-audit`, and the declared TLSF notice, and must not
contain Python driver artifacts such as `*.py`, `*python*`, or
`print_service.py`. The package builder runs the staged selftests and native
audit before creating the archive, then inspects the archive for the same
native/no-Python invariant; the PowerShell release builder extracts the
archived copies and runs the shell-only smoke/release-gate/init/native-audit
checks again before accepting the release package.
Packages default to `DENEB_RELEASE_CHANNEL=experimental`; `nightly` or
`stable` builds must provide `DENEB_PRINTSVC_STOCK_SUMMARY` and
`DENEB_PRINTSVC_NATIVE_SUMMARY`, and the package builder verifies the stock
summary with `deneb-printsvc-smoke-verify --stock --resources`, verifies the
native summary with `deneb-printsvc-smoke-verify --full`, then applies the
strict `deneb-printsvc-smoke-compare --require-reduction` resource gate before
creating the archive. The PowerShell release entry point exposes the same gate
as `-ReleaseChannel`, `-PrintsvcStockSummary`, and `-PrintsvcNativeSummary`.
The native source audit checks that both release entry points retain this
non-experimental live-evidence boundary, and
`deneb-printsvc-release-gate-selftest` behaviorally checks invalid channels,
stable builds without summaries, missing nightly summary files, malformed stock
summaries, and malformed native summary files. It uses
`DENEB_PACKAGE_VERSION_OVERRIDE` so these expected-failure package-builder
checks run in isolated staging and clean that staging/output afterward.
Both the shell package builder and PowerShell wrapper inspect the archived
manifest so the channel and native-printsvc evidence boundary travel with the
artifact.
During install, the update script rejects Python driver artifacts in
`/tmp/update`, runs the packaged native audit over the unpacked update,
requires the packaged manifest to carry the native-printsvc experimental status
plus non-experimental evidence gate, preserves that manifest at
`/etc/deneb/manifest.txt`, preserves the release-gate evidence selftest, and
runs the installed smoke-tool, native binary CLI, native-audit, and
init-handoff selftests before completing the update. The installed init
selftest validates `/etc/init.d/deneb-printsvc` and the generated
`/etc/init.d/printserver` shim when package/source paths are absent. The
installed release-gate selftest validates `/etc/deneb/manifest.txt` plus the
installed verifier/comparator/audit tools when source-only `ui/build-package.sh`
is not present on target.

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
Process RSS samples are matched by executable name, not substring, so the smoke
harness shell is not counted as `deneb-printsvc` resource evidence.
Release packages also install `/usr/bin/deneb-printsvc-stock-baseline`, a
guarded shell-only helper for collecting paired stock baseline summaries. It
requires `--allow-stock-switch` or `DENEB_PRINTSVC_STOCK_BASELINE_OK=1`, starts
the backed-up stock `printserver` init script or `/rom/etc/init.d/printserver`,
runs `deneb-printsvc-smoke --stock`, and restores native `deneb-printsvc` in an
EXIT trap unless `--no-restore-native` is explicitly supplied. During the stock
smoke window it deletes the active native procd service and runs a scoped native
guard so delayed `deneb-printsvc` respawn cannot contaminate stock process
evidence. Treat it as a supervised service-switch test, not a passive
measurement: the stock driver startup path runs the motion-controller
verification path before opening the serial stack.
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
Use `--make-complete-fixture PATH [CYCLES]` to generate a bounded old-Marlin
completion fixture. It homes Z once, switches to relative mode, performs small
relative `G1 Z-0.20 F30` moves away from the homed Z-max position, restores
absolute mode, and emits `M114`. It does not heat, extrude, dwell, use newer
Marlin commands, or command farther into the Z homing/max direction after
`G28 Z`. The cycle count is capped at 480, which keeps total travel to 96 mm
away from the homed Z-max position while providing enough runtime for active
pause/resume, Cura, completion, and sequence-wrap smoke evidence.
This mode writes the fixture and exits; run the resulting `--complete-job`
phase only under supervision because it moves the Z axis.
`--complete-job` waits up to 300 seconds by default
(`--completion-timeout SEC` or `DENEB_PRINTSVC_SMOKE_COMPLETION_TIMEOUT`
override this) before failing with the current `/printer/status` and
`/printer` root bodies in the summary and attempting a normal API abort. It
records a successful non-dwell fixture check before upload and rejects
dwell/M400-only completion fixtures; use a bounded fixture generated with
`--make-complete-fixture` or another short safe print, so live completion
evidence cannot be confused with an indefinite firmware wait or a temperature
poll loop.

Risky phases are explicit:

```sh
deneb-printsvc-smoke --native
deneb-printsvc-smoke --boot-sync
deneb-printsvc-smoke --physical-ok --native --heat
deneb-printsvc-smoke --physical-ok --native --motion
deneb-printsvc-smoke --physical-ok --native --macro bed_down
deneb-printsvc-smoke --physical-ok --native --local-job /mnt/usb/local-test.gcode
deneb-printsvc-smoke --make-active-fixture /tmp/deneb-active-z.gcode 120
deneb-printsvc-smoke --make-preheat-abort-fixture /tmp/deneb-preheat-abort.gcode
deneb-printsvc-smoke --physical-ok --native --job /home/3D/test.gcode --pause-resume
deneb-printsvc-smoke --physical-ok --native --preheat-abort /tmp/deneb-preheat-abort.gcode
deneb-printsvc-smoke --physical-ok --native --active-abort /tmp/deneb-active-z.gcode --active-abort-delay 20
deneb-printsvc-smoke --physical-ok --native --cura-job /tmp/deneb-active-z.gcode
deneb-printsvc-smoke --make-complete-fixture /tmp/deneb-complete-z.gcode 80
deneb-printsvc-smoke --physical-ok --native --complete-job /tmp/deneb-complete-z.gcode
deneb-printsvc-smoke --native --restart
deneb-printsvc-smoke --native --summary /tmp/native-printsvc.summary
deneb-printsvc-stock-baseline --allow-stock-switch --summary /tmp/stock-printsvc.summary -- --boot-sync --firmware-proof
```

Use only under supervision with clear motion axes and a ready power cutoff.
Any phase that heats, homes, moves axes, starts a print job, or sends a macro
action fails closed unless `--physical-ok` or
`DENEB_PRINTSVC_SMOKE_PHYSICAL_OK=1` is set. Prefer single-purpose physical
checks over a large bundled run. If more than one physical phase is requested
in a single invocation, the harness also requires `--physical-bundle-ok` or
`DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK=1`; this is intentionally a second
acknowledgement because bundled validation can combine homing, heat, upload,
motion, pause/resume, abort, and completion behavior. Stop immediately on any
unexpected homing, endstop, grinding, or motion-limit behavior.
Macro, local-job, web job, preheat-abort, active-abort, Cura job, and
completion-job phases now run a guarded `z_home` pre-home step before the phase
continues and record `reason=pre_physical_home` evidence in the summary. Use
`--prehome-action home` only when a test explicitly needs all-axis homing and
the X/Y travel path is clear.
Every physical phase also records a `phase=*-safety kind=physical` plan before
it runs. The plan names the axes involved, required homing, expected
travel/range, and stop conditions; the verifier and native audit selftests
reject full evidence that omits these records. These records describe the
intended safety envelope and do not replace supervised live observation.
Use the built-in fixture generators for Section 8 physical evidence whenever a
job body is not supplied by the exact workflow under test. `--make-active-fixture`
writes a bounded Z-only, no-heat, no-extrusion job that relies on the harness
pre-home step and moves only away from homed Z max. `--make-preheat-abort-fixture`
writes low bed/nozzle targets followed by a heater wait so Stop behavior can be
checked during preparation before any print motion is expected.
`--make-representative-fixture` writes a bounded Cura-style XYZ job with no
heat, no extrusion, no dwell, and an embedded
`DENEB_REPRESENTATIVE_XYZ_FIXTURE=1` marker. Any smoke phase that consumes that
fixture fails closed unless `--prehome-action home` is set, so representative
geometry evidence cannot run after only Z homing.
Abort-style job phases keep their immediate abort-requested and draining
snapshots, then poll `/printer/status` and `/printer` until native status is
`idle` with `native_active:false` and `native_stop_allowed:false` before taking
the final `*-aborted` snapshot. `--abort-settle-timeout` bounds that wait
instead of relying on a fixed sleep, so slow target cleanup is recorded as
evidence and real stuck-abort states fail the run.
`--local-job` requires `--native`, runs the native
`deneb-printsvc --local-job-smoke` path through the shared IPC frame helper, and
proves native route ownership, local/USB job acceptance with native `pre_print`
active/stop-allowed state, abort, and final native `idle` inactive/stop-disabled
state without routing through the legacy Python driver. `--job` is the
pause/resume abort-path exercise, `--preheat-abort` catches stop behavior
during preparation, `--active-abort` waits for the configured delay before
capturing active-printing evidence and aborting, and `--complete-job`
intentionally waits for a short print to
leave the active-job API without issuing an abort.

Verify a captured summary on-device without Python:

```sh
deneb-printsvc-smoke-verify /tmp/deneb-printsvc-smoke.summary
deneb-printsvc-smoke-verify --stock --resources /tmp/stock-printsvc.summary
deneb-printsvc-smoke-verify --native --idle --heat --motion --macro --local-job --job --preheat-abort --active-abort --cura-job --pause-resume /tmp/native-printsvc.summary
deneb-printsvc-smoke-verify --native --complete-job --restart --resources --boot-sync /tmp/native-printsvc.summary
deneb-printsvc-smoke-compare /tmp/stock-printsvc.summary /tmp/native-printsvc.summary
deneb-printsvc-smoke-compare --require-reduction /tmp/stock-printsvc.summary /tmp/native-printsvc.summary
deneb-printsvc-smoke-selftest
deneb-printsvc-cli-selftest /usr/bin/deneb-printsvc
deneb-printsvc-init-selftest
deneb-printsvc-release-gate-selftest
deneb-printsvc-native-audit-selftest
```

For job runs the verifier requires `printing` while active, `paused` after a
pause command, and `idle` after abort or natural completion. Completion runs
may show either an active `printing` snapshot before settling, or a fast
completed job that is already `idle` with native active/stop flags false and
zero completion-wait elapsed time. Active-print abort runs must also show
`printing` with native
active/stop-allowed flags before abort and `idle` with both flags false after
abort. It also checks native active/stop-allowed flags during active, preheat,
paused, and resumed snapshots, then requires those flags to be false after
abort or completion.
The 2026-06-08 supervised SSH validation originally used a bounded Z fixture
with repeated relative Z traffic for active abort. Follow-up validation found
that UM2C Z homes to max travel, so generated completion fixtures now use
bounded `G1 Z-0.20 F30` moves away from the homed max position. Later
full-matrix work also exposed active/abort ProtoError desync and stale native
status risk, so the earlier active-abort evidence is no longer accepted as
Section 8 proof. Completion evidence and any future active-abort refreshes must
use corrected bounded fixtures and must settle to idle with native active/stop
flags false without manual recovery.
The old positive-Z fixture timed out at the Z max travel boundary, so
completion evidence must be regenerated with the corrected away-from-max fixture
before Section 8 can be marked complete.
Native completion also deliberately waits for finish cleanup to drain before
reporting idle, so the live verifier samples the active/stop-allowed state
during planner-drain cleanup instead of accepting an immediate EOF-to-idle
transition.
The June 10, 2026 generated representative Cura-cluster smoke passed after the
abort-settle harness update: all-axis prehome, bounded XYZ moves, active
`printing` with Stop allowed, cluster DELETE abort, `aborting` with Stop
disabled while cleanup drained, `cura-job-aborted-wait elapsed=10 rc=0
status=idle`, and final native `idle` with active/Stop flags false. This is
representative generated geometry proof for the cluster API path, not a full
desktop Cura client or arbitrary slicer-output matrix.
The later June 10 `d82245c` redeploy also passed the stricter heat,
Stop-action abort, and local/USB native smokes. `--native --heat` showed
40 C / 50 C low targets as `printing` with native active/Stop flags true, then
cooldown idle with both flags false. `--native --active-abort` used the
representative fixture with all-axis prehome and the UI/API stop action,
observed active `printing` with Stop allowed, `aborting` with Stop disabled,
and final idle with active/Stop false. `--native --local-job` proved native
USB/local `pre_print` acceptance is stoppable and aborts back to idle.
The final June 10 `d82245c` completion/resource refresh fixed the remaining
EOF completion race by keeping active state when the G-code stream is exhausted
but Marlin flow packets remain in flight. Completed native resource summaries
are now rejected unless the final completion row has drained flow
(`flow_inflight:0`, `flow_resend:0`). The accepted
`/tmp/deneb-native-resources-final.summary` matched the paired stock final Z
height and drained without resend debt.
Do not raise `DENEB_PRINTSVC_STREAM_WINDOW` or shorten finish-drain timing as a
throughput shortcut without new hardware proof. A window-6 trial produced
resend debt and partial Z completion, and a fast-finish trial reported idle
before the printer reached the expected final Z. The conservative stream window
4 and finish drain 8/3 are currently part of the safety contract.
One stock-parity detail matters for long jobs: the Python driver wraps Marlin
sequence numbers from 254 back to 0 and never sends 255. Native flow control
uses the same 0..254 ring and ACK ordering across wrap, with host tests for 260
sent commands and for an ACK at 0 draining older 253/254 packets. This should
be preserved when tuning throughput, because long resource fixtures cross the
sequence boundary.
`--idle` requires the initial status sample to be `idle` and the initial
printer-root native active/stop flags to be false.
Heat, motion, and macro verification require their snapshot status and printer
root records so those phases prove backend state sampling, not only command
acceptance.
`--resources` requires initial/final memory, uptime, CPU, load, process RSS
samples, native `deneb-printsvc` driver RSS when native mode is required, and
a bytes/elapsed/bytes-per-second throughput record.
`--boot-sync` requires a successful native-only route/status readiness record
with elapsed and uptime-delta seconds, a scalar status value, and native-only
status-body evidence.
`--native` also requires native-only route body evidence, native-only route
evidence in captured status bodies, native `deneb-printsvc` process evidence,
and no captured native process sample with literal stock `print_service.py`.
It intentionally does not treat the `print_service_py=0` diagnostic field as a
stock process sample.
The June 9, 2026 observe-only installed-package run of
`Deneb_Update_8816c0b.deneb` passed installed CLI/init/release-gate/native-audit
selftests and
`deneb-printsvc-smoke-verify --native --idle --restart --boot-sync`. That run
proved native-only route, boot-sync readiness, service restart recovery, idle
`native_active:false` / `native_stop_allowed:false`, `print_service_py=0`, and
nonzero ambient bed/nozzle readings around 25.5 C / 28.3 C. It did not run
physical heat, motion, Cura, pause/resume, abort, completion, or throughput
phases.
The June 10, 2026 `Deneb_Update_d82245c.deneb` observe-only stock/native pair
verified the stock-baseline collection lane on hardware:
`/tmp/deneb-stock-d82245c.summary` passed
`deneb-printsvc-smoke-verify --stock --firmware-proof` with stock
`print_service.py` present, native `deneb-printsvc` absent, and ambient
bed/nozzle/topcap telemetry around 28.3 C / 31.8 C / 28.0 C. The paired native
`/tmp/deneb-native-d82245c-observe.summary` passed
`deneb-printsvc-smoke-verify --native --idle --boot-sync --client-proof
--firmware-proof` with native ownership and final idle active/Stop flags false.
This is observe-only firmware/temperature parity evidence, not the required
stock/native `--resources` release gate.
The compare tool reports before/after deltas for system memory,
driver-process RSS, CPU jiffies, CPU jiffies consumed between initial/final
samples, boot-sync elapsed time, and print throughput. It fails if the stock summary lacks
initial/final `print_service.py` process evidence, if CPU or throughput
intervals are not positive, or if the native summary lacks native-only route
evidence in route diagnostics or any required status lifecycle body, lacks
scalar boot-sync status plus native-only boot-sync status-body evidence,
reports a wrong lifecycle status value, contains a stock `print_service.py` process
sample, lacks native local/USB IPC job acceptance and stop-state evidence,
lacks active-print abort evidence, lacks `deneb-printsvc` process ownership,
or lacks per-lifecycle native stop-safety flags.
Use `--require-reduction` for the release-decision pass; it fails unless native
system memory, driver-process RSS, CPU interval, and boot-sync elapsed time are
lower than the stock summary, and unless native throughput is at least stock.
The latest paired stock/native resource run passed the memory, driver RSS, CPU,
and boot-sync clauses but still failed throughput: stock measured 41 B/s and
native measured 31 B/s. That means the strict non-experimental release gate is
working as intended and remains closed until throughput is improved without
regressing completion correctness.
The selftest is synthetic and does not replace live hardware evidence; it
builds stock/native summary fixtures and runs the full verifier plus comparator
so the shell evidence gates can be tested without Python. It also runs the
live smoke harness' `--summary-parser-selftest` mode to cover scalar status
extraction without touching hardware. It also checks that
missing native stop-safety evidence, missing status-body native-route evidence
in both verifier and comparator paths, missing native-route evidence in a
single comparator lifecycle status snapshot, a wrong single-phase lifecycle
status value, missing active-abort status or stop-safety evidence, boot-sync
summaries that put the full status response into
`status=` or omit `status_body` native-route proof, missing natural-completion
active or fast-completed idle status evidence, missing native local/USB job evidence, a non-native-only route diagnostic, a returned stock
`print_service.py` process in a native run,
missing stock `print_service.py` baseline evidence, missing native
`deneb-printsvc` RSS evidence, and zero-throughput records are rejected.
The CLI selftest runs the actual `deneb-printsvc` binary entry points without
opening the motion serial device, proving `--smoke-test` and the native
`--local-job-smoke` accepted/aborted stop-state rows.
The init selftest checks `deneb-printsvc.init`, installer source, the
installer-generated printserver heredoc, and generated printserver shims for
exact stock `print_service.py` cleanup, stale PID cleanup, native ownership
markers, cleanup ordering, and absence of Python driver launch commands.
