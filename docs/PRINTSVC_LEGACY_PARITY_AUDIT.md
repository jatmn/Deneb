# Native Print Service Legacy Parity Audit

This audit tracks what the native `deneb-printsvc` replacement is allowed to
claim against the stock Python `marlindriver` source. The stock Python files are
read-only reference material. A checked Section 8 item should point to concrete
native code, host tests, package gates, or live hardware evidence; broad parity
is not proven by a parser or harness existing.

## Source Anchors

| Stock source | Legacy behavior | Native claim status |
| --- | --- | --- |
| `rootfs/home/cygnus/marlindriver/print_service.py` | Binds command REP `tcp://127.0.0.1:5556`, status PUB `tcp://127.0.0.1:5555`, publishes topic `10001`, accepts `GCODE`, `MACRO`, `JOB`, `ABORT`, `PAUSE`, and `RESUME`, and publishes `firmware` from `marlin-version` defaulting to `none`. | IPC framing and stock-shaped status serialization can be claimed from native code/tests. Client behavior parity remains open until coordinator, LCD, web/API, Cura, and Digital Factory paths are proven on hardware without stock Python fallback. |
| `rootfs/home/cygnus/marlindriver/marlin_protocol.py` | When out of sync, schedules `M105` and `M114`; handles compact sync ACK/reject packets, assumed receive-buffer free space, and resend requests. It does not show normal-loop `M115` scheduling. | Native `M105`/`M114` recurring polling and flow-control tests can be claimed. Native `M115` probing is a Deneb diagnostic extension, not stock parity. |
| `rootfs/home/cygnus/marlindriver/marlin_companion/protocol.py` | Parses `MACHINE_TYPE`, `PCB_ID`, and quoted `BUILD` version output when observed and reports `marlin-version`. | Native version parser support can be claimed when host-tested. Live firmware metadata parity must compare stock and native on the same firmware; `firmware:"none"` is acceptable if stock also reports `none`. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Rewrites `M190` to `M140` and `M109` to `M104`, treats `G280` through a prime replacement path, sends periodic priority `M105`/`M114`, and updates progress/finish callbacks. | Native now has a shared `gcode_rewrite.*` helper with host tests for `M117` skip, `M109` to `M104`, `M190` to `M140`, `G280` expansion, streamed job `M190` service-side waiting, raw multi-line `GCODE` wait-before-next-command sequencing, and macro-level heater wait callbacks. Broad parity remains open for callback timing and hardware proof where physical heat/motion is involved. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Abort cleanup restores paused queue if needed, aborts the normal queue, then sends relative XY wipe, Z move, `G28 X Y`, `G28 Z`, heater/fan off, `M400`, and motor release. | Native intentionally diverges for safety by avoiding the stock duplicate/unsafe homing cleanup. Explicit and latched native aborts now route through the service abort owner so status remains aborting and active identity is retained until cleanup drains, then clears to idle. Hardware proof is still required for physical active/preheat abort motion. |
| `rootfs/home/cygnus/marlindriver/print_service.py` | Completion callback clears the active service file/type after the executor's final queue-completed callback. | Native finish cleanup now keeps status active until the finish policy/drain completes, then marks complete and clears active file/source/UUID/heater targets with host tests. Hardware completion proof remains open. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Pause saves position with `M114`, retracts/wipes/parks/cools, waits with `M400`, then resumes through reheat, XY/Z restore, extrusion restore, `G10`, `G92`, and `M105`. | Native pause/resume policy can claim stock-derived design where host-tested. Active pause now requests and drains `M114` before saving the restore position and starting cleanup, matching the stock callback ordering. Hardware parity remains open until active-print pause/resume is proven with safe motion and correct stop/status transitions. |
| `rootfs/home/cygnus/marlindriver/marlin_datalink.py` | Keeps pending packets, sends raw bytes, handles resend by sending two `0xff` bytes then replaying pending packets, clears resend state on acknowledge, and ignores nested rejects while resending. | Native flow-control behavior can be claimed where host tests cover ACK/reject/resend and repeated-reject failure handling. Active-print desync behavior still needs live proof because it affects physical job safety. |

## Current No-Overclaim Rules

- A parser recognizing a Marlin line is not evidence that native runtime requests that line on the physical printer.
- A smoke harness or verifier existing is not evidence that the corresponding live phase has passed.
- A static no-Python package audit is not evidence that every client stopped depending on old Python-shaped bridge state.
- A stock-compatible default such as `firmware:"none"` is not a failure by itself; compare stock and native under the same firmware state before treating it as a defect.
- A `z_home` pre-home guard is not XY safety proof. Motion evidence must identify axes moved, required homing, expected direction/range, and stop conditions. The smoke harness now requires those records before accepting physical-phase summaries, but live observation is still required.

## Current Live Evidence

- June 9, 2026 observe-only deployment evidence: `dist/Deneb_Update_8816c0b.deneb`
  installed over SSH, rebooted, and passed installed CLI, init-handoff,
  release-gate, and native-audit selftests. The installed process check showed
  `/usr/bin/deneb-printsvc` running and no `print_service.py`.
- The installed observe-only smoke run
  `/usr/bin/deneb-printsvc-smoke --native --restart --boot-sync` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --restart --boot-sync`.
  It proved native-only route, boot-sync readiness, service restart recovery,
  idle status, `native_active:false`, `native_stop_allowed:false`, and
  `print_service_py=0`.
- The same smoke run proved temperature reporting was alive at idle: bed current
  was about 25.5 C with target 0.0 C, and nozzle current was about 28.3 C with
  target 0.0 C. This closes the observe-only `0/0 C` regression check, not any
  physical heating phase.
- The smoke harness process sampler now records executable-level samples so the
  smoke wrapper shell is not counted as `deneb-printsvc` RSS evidence, and the
  verifier rejects literal `print_service.py` process samples without matching
  `print_service_py` summary fields.
- Host-side smoke and native-audit selftests now prove full physical summaries
  must include `phase=*-safety kind=physical` records with axes, required
  homing, expected travel/range, and stop conditions. This closes the harness
  safety-audit contract, not the live physical proof.

## Open Parity Work

- `G280`, `M109`, `M190`, raw multi-line `GCODE` wait sequencing, stock-order pause position capture, completion active-identity cleanup, and abort cleanup-before-idle timing have host-tested native coverage but still need hardware proof where they drive physical heat or motion.
- Prove native client behavior on hardware for coordinator, LCD UI, web/API, Cura LAN, and Digital Factory without stock Python fallback or stale pending-job state.
- Regenerate supervised live active-abort, preheat-abort, pause/resume, completion, Cura, and resource comparison evidence using the new physical safety-plan records.
- Keep Section 8 open until every checked live claim points to current hardware evidence, not just host tests or package gates.
