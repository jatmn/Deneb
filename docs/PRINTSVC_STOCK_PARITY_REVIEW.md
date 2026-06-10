# Native Print-Service Stock Parity Review

This review records the source-level comparison between stock Python
`marlindriver` and native `deneb-printsvc`. The Python source is reference-only;
native behavior is either stock-compatible, host-tested, or deliberately
different for safety/resource reasons.

## Item-by-item review

| Stock source | Stock behavior | Native owner | Review decision |
| --- | --- | --- | --- |
| `rootfs/home/cygnus/marlindriver/print_service.py` | Command REP uses stock command verbs `GCODE`, `MACRO`, `JOB`, `ABORT`, `PAUSE`, and `RESUME`; status PUB emits topic `10001` with stock-shaped JSON. Busy commands are rejected except abort/pause/resume. | `printsvc/src/command*.c`, `printsvc/src/status.c`, `printsvc/src/ipc_*`, shared route helpers. | Stock-compatible IPC shape is implemented and host/audit tested. Client behavior still needs hardware proof per surface, so this is contract parity only. |
| `rootfs/home/cygnus/marlindriver/print_service.py` | Status publishes only after executor sync; firmware defaults to `marlin-version` or `none`. | `printsvc/src/service.c`, `printsvc/src/status_parser.c`, `printsvc/src/status.c`, smoke `--firmware-proof`. | Native preserves the field/default and parses stock version lines. Native startup sends bounded `M115` as a diagnostic extension; stock normal-loop only schedules `M105`/`M114`, while stock `init.gcode` contains `M115`. A June 10, 2026 observe-only stock/native pair captured matching firmware-default behavior plus nonzero bed/nozzle/topcap ambient telemetry; broad parity still needs the full resource and client-flow evidence. |
| `rootfs/home/cygnus/marlindriver/marlin_protocol.py` | When out of sync, repeatedly sends `M105` and `M114`; normal periodic status sends priority `M105` then `M114`. | `printsvc/src/service.c`, `printsvc/src/motion_runtime.c`, flow-control tests. | Native recurring status polling keeps the stock-shaped `M105`/`M114` cadence after startup probing. |
| `rootfs/home/cygnus/marlindriver/marlin_companion/protocol.py` | Parses `MACHINE_TYPE`, `PCB_ID`, quoted `BUILD`, M105 temperatures including bed/topcap, M114 position/R0, and G28 home-distance telemetry. | `printsvc/src/status_parser.c`, `printsvc/tests/test_printsvc.c`. | Host parser coverage exists for stock telemetry forms including compact old-Marlin temperature reports. The `d82245c` observe-only stock/native pair proves current ambient telemetry collection on this printer, not physical heat/motion parity. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Rewrites `M190` to `M140`, `M109` to `M104`, strips comments, ignores `M117`, and expands/skips `G280` via the stock prime path. | `printsvc/src/gcode_rewrite.c`, `printsvc/src/gcode_stream.c`, heater-wait code. | Host-tested parity exists for rewrite/wait sequencing. Physical heat/motion proof remains required before claiming live parity for these commands. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | The final normal-queue command callback invokes queue completion, marks executor idle, records finish time, then `print_service.py` clears active file/type. | `printsvc/src/job_streamer.c`, `printsvc/src/job_control.c`, `printsvc/src/job_lifecycle.c`. | Native deliberately waits for finish policy and planner-drain evidence before clearing active state. It also refuses to leave active state at G-code EOF while Marlin flow packets remain in flight, which matches the stock intent that completion follows the final queued callback. Host tests and bounded Z-only completion/resource hardware proof exist; representative slicer completion remains open. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Abort restores a paused queue if needed, aborts it, then runs relative wipe/retract, `G28 X Y`, `G28 Z`, heater/fan off, `M400`, and `M84`; only the final `M84` callback marks aborted/idle and lets `print_service.py` clear file/type/req. | `printsvc/src/job_control.c`, `printsvc/src/motion_policy.c`, shared print-state rules, smoke verifier/comparator. | Native intentionally diverges for safety: no stock XY/Z homing cleanup after abort. Native keeps `aborting` with Stop disabled until cleanup drains, then clears active identity. Host tests, stricter smoke gates, and bounded hardware proof exist; representative geometry proof remains open. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Pause saves the active queue, sends `M114`, then on callback sends another `M114` to save position before wipe/retract/park/cool. | `printsvc/src/pause_resume_control.c`, `printsvc/src/motion_policy.c`, tests. | Native is stock-derived and now requires a fresh `M114` position report before saving the restore point. This avoids stale coordinate reuse while preserving the stock ordering intent. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Resume reheats with `M109`, restores E mode, X/Y, Z, extrusion, R0, E, then sends `M105` before restoring the paused queue and reporting resumed. | `printsvc/src/pause_resume_control.c`, `printsvc/src/motion_policy.c`, heater wait and lifecycle tests. | Native mirrors the stock resume sequence with clamped coordinates and native service-owned policy dispatch. Host tests and bounded hardware pause/resume proof exist; Cura-started representative pause/resume remains open. |
| `rootfs/home/cygnus/marlindriver/marlin_datalink.py` | Handles packet send/receive, ACK/reject, resend by sending two `0xff` bytes before replaying pending packets, and ignores nested rejects during resend. | `printsvc/src/flow_control.c`, `printsvc/src/motion_sender.c`, `printsvc/src/motion_runtime.c`, tests. | Native has host coverage for ACK/reject/resend and active-job sequence resync policy. Native now also matches stock `MarlinProtocolNode.send()` sequence wrapping by using 0..254 and skipping 255, with host coverage for ACK-through behavior across the wrap. Live desync/fault parity remains open because it is a physical reliability path. |

## Deliberate divergences

- Native abort does not copy stock's post-abort `G28 X Y` / `G28 Z` cleanup.
  That stock sequence was observed as unsafe for unknown print geometry on this
  machine. Native uses planner wait, cooldown, and stepper release instead.
- Native startup `M115` probing is diagnostic, bounded, and not presented as
  stock normal-loop behavior. Stock source parses version output and stock
  `init.gcode` contains `M115`, but normal periodic Python polling is
  `M105`/`M114`.
- Native finish cleanup intentionally keeps active print state until finish
  policy and planner-drain checks complete, preventing premature idle UI.
- Native stream window remains below stock Python's receive-buffer size after
  live hardware showed that a window of 6 caused resend debt and partial
  completion on this old Marlin path. This is a deliberate safety/stability
  divergence until equivalent throughput can be proven without flow debt.

## Remaining proof after source review

- Full stock/native resource, boot-time, and throughput comparison using the
  guarded stock-baseline helper plus a full native smoke summary remains open.
  A current native long-completion run passed with sequence-wrap parity,
  drained flow, and positive Z travel, but the paired stock run reported
  completion without moving after the running snapshot. Throughput samples are
  no longer accepted unless `phase=complete-job-position` proves positive Z
  travel on both sides.
- Representative Cura/slicer geometry for completion, pause/resume, and abort.
- LCD UI, Web UI, Cura client, coordinator, and Digital Factory lifecycle proof
  against native service without Python fallback.
