# Native Print-Service Stock Parity Review

This review records the source-level comparison between stock Python
`marlindriver` and native `deneb-printsvc`. The Python source is reference-only;
native behavior is either stock-compatible, host-tested, or deliberately
different for safety/resource reasons.

## Item-by-item review

| Stock source | Stock behavior | Native owner | Review decision |
| --- | --- | --- | --- |
| `rootfs/home/cygnus/marlindriver/print_service.py` | Command REP uses stock command verbs `GCODE`, `MACRO`, `JOB`, `ABORT`, `PAUSE`, and `RESUME`; status PUB emits topic `10001` with stock-shaped JSON. Busy commands are rejected except abort/pause/resume. | `printsvc/src/command*.c`, `printsvc/src/status.c`, `printsvc/src/ipc_*`, shared route helpers. | Stock-compatible IPC shape is implemented and host/audit tested. Client behavior still needs hardware proof per surface, so this is contract parity only. |
| `rootfs/home/cygnus/marlindriver/print_service.py` | Status publishes only after executor sync; firmware defaults to `marlin-version` or `none`. | `printsvc/src/service.c`, `printsvc/src/status_parser.c`, `printsvc/src/status.c`, smoke `--firmware-proof`. | Native preserves the field/default and parses stock version lines. Native startup sends bounded `M115` as a diagnostic extension; stock normal-loop only schedules `M105`/`M114`, while stock `init.gcode` contains `M115`. Paired observe-only stock/native evidence captured matching firmware-default behavior plus nonzero bed/nozzle/topcap ambient telemetry; broad parity still needs the open client-flow and long-soak evidence. |
| `rootfs/home/cygnus/marlindriver/marlin_protocol.py` | When out of sync, repeatedly sends `M105` and `M114`; normal periodic status sends priority `M105` then `M114`. | `printsvc/src/service.c`, `printsvc/src/motion_runtime.c`, flow-control tests. | Native recurring status polling keeps the stock-shaped `M105`/`M114` cadence after startup probing. |
| `rootfs/home/cygnus/marlindriver/marlin_companion/protocol.py` | Parses `MACHINE_TYPE`, `PCB_ID`, quoted `BUILD`, M105 temperatures including bed/topcap, M114 position/R0, and G28 home-distance telemetry. | `printsvc/src/status_parser.c`, `printsvc/tests/test_printsvc.c`. | Host parser coverage exists for stock telemetry forms including compact old-Marlin temperature reports. The `d82245c` observe-only stock/native pair proves current ambient telemetry collection on this printer, not physical heat/motion parity. |
| `rootfs/home/cygnus/coordinator/handlers/printhandling.py`, `rootfs/home/cygnus/marlindriver/marlin_executor.py`, `rootfs/home/cygnus/marlindriver/marlin_companion/stream.py` | Stock print prepare runs `home_and_center_head.gcode`, releases Z with `M18 Z`, waits for motion to finish, heats/extracts, then re-homes Z with `G28 Z` before `JOB`. Stock `JOB` startup then optionally sends filament-to-tip priming when the first 50 non-comment file lines do not contain `G280`, sends `G90`, `M82`, `G92 E0`, and `G0 F9000`, and expands `G280` through the stock prime path. | `printsvc/src/job_streamer.c`, `printsvc/src/gcode_stream.c`, `printsvc/src/gcode_rewrite.c`, heater-wait code. | Native implements the stock-derived startup/`G280` boundary, but deliberately omits the normal-print `M18 Z` release and follow-up `G28 Z` re-home. 2026-06-13 target testing showed the stock-derived double-Z-home sequence started safely but wasted time; host tests now cover the optimized sequence: barrier-drained `G28`, `G0 X105 Y0 F9000`, heat wait, stock startup, first-50-line `G280` detection, no-`G280` filament-to-tip priming, and command-level `M190`/`M109` waits before further streaming. Target proof is still required before marking the double-Z-home fix closed. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | The final normal-queue command callback invokes queue completion, marks executor idle, records finish time, then `print_service.py` clears active file/type. | `printsvc/src/job_streamer.c`, `printsvc/src/job_control.c`, `printsvc/src/job_lifecycle.c`. | Native deliberately waits for finish policy and planner-drain evidence before clearing active state. It also refuses to leave active state at G-code EOF while Marlin flow packets remain in flight, which matches the stock intent that completion follows the final queued callback. Host tests and bounded Z-only completion/resource hardware proof exist; representative slicer completion remains open. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Abort restores a paused queue if needed, aborts it, then runs relative wipe/retract, `G28 X Y`, `G28 Z`, heater/fan off, `M400`, and `M84`; only the final `M84` callback marks aborted/idle and lets `print_service.py` clear file/type/req. | `printsvc/src/job_control.c`, `printsvc/src/motion_policy.c`, shared print-state rules, smoke verifier/comparator. | Native now uses a stock-derived abort cleanup policy: relative wipe/retract, `G28 X Y`, `G28 Z`, heater/fan off, `M400`, and `M84`, while keeping the UI/service in `aborting` until cleanup drains. Host tests cover the sequence. Target proof remains open for the touchscreen Stop path because the 2026-06-13 user run proved the previous native package stopped without the expected park/home routine. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Pause saves the active queue, sends `M114`, then on callback sends another `M114` to save position before wipe/retract/park/cool. | `printsvc/src/pause_resume_control.c`, `printsvc/src/motion_policy.c`, tests. | Native is stock-derived and now requires a fresh `M114` position report before saving the restore point. This avoids stale coordinate reuse while preserving the stock ordering intent. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Resume reheats with `M109`, restores E mode, X/Y, Z, extrusion, R0, E, then sends `M105` before restoring the paused queue and reporting resumed. | `printsvc/src/pause_resume_control.c`, `printsvc/src/motion_policy.c`, heater wait and lifecycle tests. | Native mirrors the stock resume sequence with clamped coordinates and native service-owned policy dispatch. A 2026-06-13 touchscreen test on package `68af57c` proved Pause and Stop behavior but exposed a cold-resume blocker: the nozzle target was cooled to zero and Resume moved without reheating. Package `072edbc` changed Resume to reassert the saved nozzle target first, wait for target temperature, then restore motion. User-supervised target testing on 2026-06-13 proved Resume preheated the nozzle again, waited for temperature, returned to position, and continued printing successfully. |
| `rootfs/home/cygnus/marlindriver/marlin_datalink.py` | Handles packet send/receive, ACK/reject, resend by sending two `0xff` bytes before replaying pending packets, and ignores nested rejects during resend. | `printsvc/src/flow_control.c`, `printsvc/src/motion_sender.c`, `printsvc/src/motion_runtime.c`, tests. | Native has host coverage for ACK/reject/resend and active-job sequence resync policy. Native now also matches stock `MarlinProtocolNode.send()` sequence wrapping by using 0..254 and skipping 255, with host coverage for ACK-through behavior across the wrap. Live desync/fault parity remains open because it is a physical reliability path. |

## Deliberate divergences

- Native abort is stock-derived but not yet target-promoted: the host-tested
  policy now includes the stock `G28 X Y` / `G28 Z` cleanup, heater/fan off,
  planner wait, and `M84`. Keep target Stop proof open until this exact package
  is deployed and a supervised touchscreen Stop run confirms the expected
  park/home behavior on hardware.
- Native startup `M115` probing is diagnostic, bounded, and not presented as
  stock normal-loop behavior. Stock source parses version output and stock
  `init.gcode` contains `M115`, but normal periodic Python polling is
  `M105`/`M114`.
- Native print prepare deliberately skips the stock-derived normal-print
  `M18 Z` release and second `G28 Z`. The stock behavior was safe on target but
  caused a visible double-Z-home delay; host tests now cover the optimized
  sequence. Target proof is still required before promoting this divergence.
- Native finish cleanup intentionally keeps active print state until finish
  policy and planner-drain checks complete, preventing premature idle UI.
- Native stream window remains below stock Python's receive-buffer size after
  live hardware showed that a window of 6 caused resend debt and partial
  completion on this old Marlin path. This is a deliberate safety/stability
  divergence. A later active-loop scheduler fix improved native streaming
  without widening the window, and the current bounded stock/native comparison
  keeps window 4 as the accepted native policy.

## Remaining proof after source review

- Representative Cura/slicer geometry for completion and abort, including any
  remaining touchscreen Stop park/home edge cases.
- Target proof for the optimized print-start prepare sequence with no second
  Z home.
- LCD UI and Web UI hands-on proof against native service without Python
  fallback or stale print state.
- Desktop Cura client proof beyond generated cluster API fixtures.
- Digital Factory lifecycle proof beyond observe-only bridge status.
- Multi-hour active heat/motion/job soak evidence that explains or eliminates
  the remaining RSS/private-memory staircase.
