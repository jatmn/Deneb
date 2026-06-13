# Native Print Service Legacy Parity Audit

This audit defines what the native `deneb-printsvc` replacement may claim
against the stock Python `marlindriver` source. The stock Python files are
read-only reference material. Broad parity is never proven by a parser, a static
audit, or a host test alone.

Detailed source comparison lives in
[PRINTSVC_STOCK_PARITY_REVIEW.md](PRINTSVC_STOCK_PARITY_REVIEW.md). Current
promotion evidence lives in
[PRINTSVC_EVIDENCE_LEDGER.md](PRINTSVC_EVIDENCE_LEDGER.md).

## Source Anchors

| Stock source | Legacy behavior | Native claim status |
| --- | --- | --- |
| `print_service.py` | Binds command REP `tcp://127.0.0.1:5556`, status PUB `tcp://127.0.0.1:5555`, publishes topic `10001`, accepts `GCODE`, `MACRO`, `JOB`, `ABORT`, `PAUSE`, and `RESUME`, and publishes `firmware` from `marlin-version` defaulting to `none`. | Native IPC framing, status serialization, and command verbs are implemented and tested. Client parity stays open until LCD, web/API, Cura, and Digital Factory workflows are proven on hardware without stock Python fallback. |
| `marlin_protocol.py` | Normal loop schedules `M105` and `M114`; handles ACK/reject packets, assumed receive-buffer free space, and resend requests. It does not show normal-loop `M115` scheduling. | Native recurring `M105`/`M114` polling and flow-control behavior can be claimed where tested. Native `M115` startup/retry probing is a Deneb diagnostic extension, not stock parity. |
| `marlin_companion/protocol.py` | Parses `MACHINE_TYPE`, `PCB_ID`, and quoted `BUILD` version output when observed. | Native parser support can be claimed where host-tested. Live metadata parity requires paired stock/native capture on the same firmware. |
| `printhandling.py`, `marlin_executor.py`, `marlin_companion/stream.py` | Print prepare homes/centers, releases Z, waits, heats/extracts, re-homes Z, then `JOB` startup handles first-50-line `G280` detection, optional no-`G280` filament-to-tip priming, startup modal commands, `M190`/`M109` waits, and `G280` prime replacement. | Native `job_streamer.*`, `gcode_stream.*`, and `gcode_rewrite.*` now implement the stock-derived prepare/startup path with host coverage. 2026-06-13 target testing showed safe startup, but observed a double-Z-home delay: the printer homes XYZ, moves to the prepare position, then re-homes Z. Keep this open as a prepare optimization task before changing the stock-derived sequence. |
| `marlin_executor.py` | Abort cleanup sends stock XY/Z cleanup and homing moves. | Native `motion_policy.c` now sends a stock-derived abort cleanup sequence with relative wipe/retract, `G28 X Y`, `G28 Z`, heater/fan off, `M400`, and `M84`. Host tests cover the command order. Target Stop proof remains open until the fixed package is deployed and the touchscreen Stop path is observed to park/home safely. |
| `print_service.py` | Completion clears active file/type after final queue-completed callback. | Native keeps active state until finish cleanup/drain completes, then clears active identity. Bounded completion proof exists; representative slicer completion remains open. |
| `marlin_executor.py` | Pause saves position with `M114`, retracts/parks/cools, then resumes by reheating and restoring position/extrusion state. | Native pause/resume follows the stock ordering shape in host tests. A 2026-06-13 touchscreen run on package `68af57c` paused and stopped correctly, but Resume attempted to restore cold after Pause cooled the nozzle. Package `072edbc` reasserts the saved nozzle target, waits for target temperature, then restores motion; user-supervised target testing on 2026-06-13 proved the nozzle reheated before returning to position and continuing. |
| `marlin_datalink.py` | Tracks pending packets, handles ACK/reject/resend, and skips sequence 255 by wrapping 254 to 0. | Native flow-control tests cover the current ring/resend behavior. Live desync/fault behavior remains safety-sensitive and must stay evidence-backed. |

## No-Overclaim Rules

- A parser recognizing a line is not proof that the physical runtime requests or
  receives that line.
- A smoke harness existing is not proof that the corresponding phase passed.
- A static no-Python audit is not proof that every client path has migrated.
- `firmware:"none"` is acceptable only when paired stock/native evidence shows
  stock also reports `none`.
- Generated fixtures, cluster API smoke, desktop Cura behavior, and arbitrary
  slicer output are separate proof classes.
- Physical motion evidence must name axes, required homing, expected range, and
  stop conditions.

## Current Accepted Evidence

Use [PRINTSVC_EVIDENCE_LEDGER.md](PRINTSVC_EVIDENCE_LEDGER.md) as the
authoritative index. As of this cleanup, accepted bounded evidence covers:

- Native route ownership and no stock Python driver process for native packages.
- Static archive/package/installer gates rejecting Python driver artifacts.
- Paired observe-only stock/native firmware and ambient telemetry.
- Bounded heat/preheat Stop, active abort, pause/resume, generated cluster API
  upload/start/abort, local/native print acceptance, and completion slices.
- Bounded stock/native resource comparison with native driver RSS reduction and
  throughput above the current comparator floor.
- Short repeated-job stability.
- Host Valgrind and sanitizer gates for the current host C test surface.
- Diagnostics log-growth mitigation.

These are bounded evidence slices. They do not close the remaining client-flow,
representative slicer, Digital Factory lifecycle, or multi-hour soak gates.

## Open Parity Work

- Prove LCD hands-on queued/start/pause/resume/abort/completion/stale-state
  workflow, including that the active Status screen exposes the combined
  Pause/Resume control and Stop runs the stock-derived park/home cleanup.
- Investigate and fix the observed print-start double-Z-home delay without
  regressing the stock-derived safe prepare/startup path.
- Prove Web UI hands-on status/control workflow.
- Prove desktop Cura client discovery, upload/start, monitor,
  pause/resume/abort/delete, and pending-job behavior.
- Prove Digital Factory job lifecycle behavior, not just bridge status.
- Prove representative real slicer output for completion, pause/resume, and
  abort.
- Prove multi-hour active heat/motion/job stability and resolve or explain the
  current RSS/private-memory staircase.
- Keep integration ownership/removal conditions in
  [PRINTSVC_INTEGRATION_AUDIT.md](PRINTSVC_INTEGRATION_AUDIT.md) aligned with
  any client-path changes.

Keep Section 8 open until every checked live claim points to current hardware
evidence, not just host tests or package gates.
