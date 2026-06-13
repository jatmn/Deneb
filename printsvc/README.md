# Deneb Print Service

`deneb-printsvc` is the native C replacement track for the stock
`/home/cygnus/marlindriver/print_service.py` service.

Current status: packaged and tested as an experimental native print backend.
It has bounded hardware proof, static package gates, host tests, and stock/native
resource evidence, but it is not stable until the open client and long-soak
gates in [docs/PRINTSVC_EVIDENCE_LEDGER.md](../docs/PRINTSVC_EVIDENCE_LEDGER.md)
are closed.

## Compatibility Boundary

The first-stage native service preserves the stock client contract:

- Status PUB: `tcp://127.0.0.1:5555`
- Command REP: `tcp://127.0.0.1:5556`
- Status topic: `10001`
- Command framing: `COMMAND<json>`
- Commands: `GCODE`, `MACRO`, `JOB`, `ABORT`, `PAUSE`, and `RESUME`

Deneb touchscreen and web/API clients route to this native endpoint through
`common/print/print_backend_route.*`.

## Source Layout

The service is split by responsibility so the replacement does not become a
single stock-driver-shaped catch-all:

- IPC and framing: `ipc_zmq.*`, `ipc_frame.*`
- Command parsing and dispatch: `command.*`, `command_dispatch.*`,
  `service_command.*`, `command_audit.*`
- Replies and errors: `command_reply.*`, `error_map.*`,
  `motion_send_error.*`
- Service state: `service.*`, `service_context.*`, `status.*`,
  `runtime_diagnostics.*`
- Marlin transport: `serial_transport.*`, `motion_runtime.*`,
  `motion_sender.*`, `marlin_packet.*`, `crc.*`, `flow_control.*`
- Status parsing: `status_parser.*`
- Jobs and lifecycle: `job_control.*`, `job_streamer.*`, `job_lifecycle.*`,
  `print_control.*`
- Pause/resume and abort policy: `pause_resume.*`,
  `pause_resume_control.*`, `motion_policy.*`
- G-code and macros: `gcode_stream.*`, `gcode_rewrite.*`,
  `gcode_control.*`, `macro_registry.*`, `macro_runner.*`,
  `macro_control.*`
- Heater waits and firmware checks: `heater_wait.*`, `motion_firmware.*`,
  `motion_observer.*`
- Diagnostics: `diagnostics_log.*`, `command_audit.*`

Shared client policy lives under `common/print/`, including command formatting,
backend route selection, print-state rules, print actions, pending-job files,
job summaries, printer responses, motion/temperature planning, material
catalog/profile helpers, diagnostics export, frame light, language, and printer
identity.

## Intentional Behavior

- Startup fails closed if `/dev/ttyS1` cannot be opened. `--dry-run` is for
  host/lab debugging only and must not be used by packaged init scripts.
- Native firmware probing adds bounded `M115` diagnostics while keeping normal
  recurring polling on the stock-shaped `M105`/`M114` cadence.
- Pause captures a fresh `M114` position before retract/park/cool so resume does
  not rely on a stale periodic position sample.
- Resume uses the saved positive job nozzle target for the leading `M109`
  reheat. If a paused position-aware job has no positive saved nozzle target,
  resume fails closed instead of restoring motion cold.
- Abort uses a stock-derived cleanup sequence with relative wipe/retract,
  `G28 X Y`, `G28 Z`, heater/fan off, `M400`, and `M84`. It keeps status in
  `aborting` until cleanup drains, then clears active identity to idle.
- Completion keeps the job active until finish cleanup and flow drain complete,
  then clears active file/source/UUID/heater state.
- G-code streaming is bounded and line-oriented; jobs and macros are not loaded
  fully into memory.
- Deneb-owned macros are resolved from `/etc/deneb/marlindriver/gcode/` and
  fail closed if missing instead of falling back to stock macro files.

## Diagnostics Log

`deneb-printsvc` writes comparison-oriented diagnostics to
`/var/log/ultimaker/deneb-printsvc.log`, falling back to `/tmp` when needed.
Normal ACK, line-number, and `flow_last_response` churn is heartbeat-throttled
so logs do not grow rapidly during healthy serial traffic. Resend, reject,
error, state change, latency, and lifecycle information remains visible.

Continue measuring diagnostics log size during active soaks.

## Host Build

```sh
cmake -S printsvc -B /tmp/deneb-printsvc-host -DBUILD_HOST_STUB=ON
cmake --build /tmp/deneb-printsvc-host
ctest --test-dir /tmp/deneb-printsvc-host --output-on-failure
```

When `sh` is available, CTest also runs shell selftests for the smoke verifier,
stability harness, CLI behavior, init handoff, release gate, native package
audit, and integration audit.

For host memory checks from WSL/Linux:

```sh
tools/deneb-printsvc-valgrind.sh
```

Run Valgrind Memcheck where practical before accepting native print-service
changes that touch streaming, parsers, lifecycle state, flow control, or shared
print-control helpers. Valgrind and sanitizer runs prove the host test surface
is clean. They do not replace target-side `/proc`, log, and hardware evidence.

## Release Package Gate

The full release build cross-compiles and packages `deneb-printsvc`:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```

Package and installer gates require:

- `deneb-printsvc` and `deneb-printsvc.init`
- Smoke, verify, compare, stability, active-soak, stock-baseline, CLI, init,
  release-gate, native-audit, and integration-audit tools
- Deneb-owned macro defaults
- Manifest fields for channel and native print-service evidence boundary
- Declared notices, including the TLSF notice
- No packaged Python driver artifacts such as `*.py`, `*python*`, or
  `print_service.py`

Native print-service packages default to `experimental`. `nightly` or `stable`
builds must provide verified stock/native summaries and pass
`deneb-printsvc-smoke-compare --require-reduction`.

## Device Smoke Harness

Release packages install `/usr/bin/deneb-printsvc-smoke` for target evidence.
Observe-only mode records route, status, process, resource, and readiness
snapshots. Physical phases fail closed unless `--physical-ok` or
`DENEB_PRINTSVC_SMOKE_PHYSICAL_OK=1` is set.

Common examples:

```sh
deneb-printsvc-smoke --native --boot-sync --client-proof --firmware-proof
deneb-printsvc-smoke --physical-ok --native --heat
deneb-printsvc-smoke --physical-ok --native --job /home/3D/test.gcode --pause-resume
deneb-printsvc-smoke --physical-ok --native --cura-job /tmp/deneb-representative-xyz.gcode --prehome-action home
deneb-printsvc-smoke --physical-ok --native --complete-job /tmp/deneb-complete-z.gcode
deneb-printsvc-stock-baseline --allow-stock-switch --summary /tmp/stock-printsvc.summary -- --boot-sync --firmware-proof
```

Fixture helpers:

- `--make-complete-fixture PATH [CYCLES]` creates bounded Z completion input.
- `--make-active-fixture PATH [CYCLES]` creates bounded active-print input.
- `--make-preheat-abort-fixture PATH` creates low-temperature preheat-abort
  input.
- `--make-representative-fixture PATH [CYCLES]` creates bounded Cura-style XYZ
  input with no heat, extrusion, dwell, or internal homing.

Representative XYZ fixtures require `--prehome-action home`; Z-only fixtures
use guarded `z_home` where appropriate. Every physical phase records a safety
plan naming axes, required homing, expected travel/range, and stop conditions.
These records are evidence metadata, not a replacement for supervised hardware
observation.

## Verification Commands

```sh
deneb-printsvc-smoke-verify /tmp/deneb-printsvc-smoke.summary
deneb-printsvc-smoke-verify --stock --resources /tmp/stock-printsvc.summary
deneb-printsvc-smoke-verify --native --idle --boot-sync --client-proof --firmware-proof /tmp/native-observe.summary
deneb-printsvc-smoke-verify --native --idle --job --pause-resume --cura-job --preheat-abort --active-abort --complete-job --resources /tmp/native-full.summary
deneb-printsvc-smoke-compare --require-reduction /tmp/stock-printsvc.summary /tmp/native-full.summary
deneb-printsvc-smoke-selftest
deneb-printsvc-cli-selftest /usr/bin/deneb-printsvc
deneb-printsvc-init-selftest
deneb-printsvc-release-gate-selftest
deneb-printsvc-native-audit-selftest
deneb-printsvc-integration-audit-selftest
deneb-active-physical-soak-runner --duration 7200 --fixture /tmp/deneb-active-physical-soak-xyz.gcode
```

## Evidence Rules

- Static audits prove source/package guardrails, not physical behavior.
- Host tests prove the host-native C surface, not live MIPS behavior.
- Generated fixtures, cluster API smoke, desktop Cura behavior, and arbitrary
  slicer output are separate proof classes.
- Observe-only runs are baseline context, not active print-service stability.
- Completion and abort evidence must settle to `idle` with
  `native_active:false`, `native_stop_allowed:false`, and no resend debt where
  required by the verifier.
- Long-soak promotion requires active heat/motion/job cycles, not only idle
  sampling.

## Open Promotion Work

Track current status in
[docs/PRINTSVC_EVIDENCE_LEDGER.md](../docs/PRINTSVC_EVIDENCE_LEDGER.md). The
native service remains experimental until these pass:

- LCD hands-on queued/start/pause/resume/abort/completion/stale-state workflow,
  including fixed-package proof that Resume reheats after Pause cooldown.
- Web UI hands-on status/control workflow.
- Desktop Cura discovery, upload/start, monitor, pause/resume/abort/delete, and
  pending-job behavior.
- Digital Factory job lifecycle.
- Representative real slicer output.
- Multi-hour active heat/motion/job stability with acceptable memory, tmpfs, and
  diagnostics-log behavior.
