# Native Print Service Legacy Parity Audit

This audit tracks what the native `deneb-printsvc` replacement is allowed to
claim against the stock Python `marlindriver` source. The stock Python files are
read-only reference material. A checked Section 8 item should point to concrete
native code, host tests, package gates, or live hardware evidence; broad parity
is not proven by a parser or harness existing.

The item-by-item source comparison is tracked in
[PRINTSVC_STOCK_PARITY_REVIEW.md](PRINTSVC_STOCK_PARITY_REVIEW.md). This audit
keeps the shorter evidence summary and the live-proof gaps.

## Source Anchors

| Stock source | Legacy behavior | Native claim status |
| --- | --- | --- |
| `rootfs/home/cygnus/marlindriver/print_service.py` | Binds command REP `tcp://127.0.0.1:5556`, status PUB `tcp://127.0.0.1:5555`, publishes topic `10001`, accepts `GCODE`, `MACRO`, `JOB`, `ABORT`, `PAUSE`, and `RESUME`, and publishes `firmware` from `marlin-version` defaulting to `none`. | IPC framing and stock-shaped status serialization can be claimed from native code/tests. Client behavior parity remains open until coordinator, LCD, web/API, Cura, and Digital Factory paths are proven on hardware without stock Python fallback. |
| `rootfs/home/cygnus/marlindriver/marlin_protocol.py` | When out of sync, schedules `M105` and `M114`; handles compact sync ACK/reject packets, assumed receive-buffer free space, and resend requests. It does not show normal-loop `M115` scheduling. | Native `M105`/`M114` recurring polling and flow-control tests can be claimed. Native `M115` probing is a Deneb diagnostic extension, not stock parity. |
| `rootfs/home/cygnus/marlindriver/marlin_companion/protocol.py` | Parses `MACHINE_TYPE`, `PCB_ID`, and quoted `BUILD` version output when observed and reports `marlin-version`. | Native version parser support can be claimed when host-tested. Live firmware metadata parity must compare stock and native on the same firmware; `firmware:"none"` is acceptable if stock also reports `none`. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Rewrites `M190` to `M140` and `M109` to `M104`, treats `G280` through a prime replacement path, sends periodic priority `M105`/`M114`, and updates progress/finish callbacks. | Native now has a shared `gcode_rewrite.*` helper with host tests for `M117` skip, `M109` to `M104`, `M190` to `M140`, `G280` expansion, streamed job `M190` service-side waiting, raw multi-line `GCODE` wait-before-next-command sequencing, and macro-level heater wait callbacks. Broad parity remains open for callback timing and hardware proof where physical heat/motion is involved. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Abort cleanup restores paused queue if needed, aborts the normal queue, then sends relative XY wipe, Z move, `G28 X Y`, `G28 Z`, heater/fan off, `M400`, and motor release. | Native intentionally diverges for safety by avoiding the stock duplicate/unsafe homing cleanup. Explicit and latched native aborts now route through the service abort owner so status remains aborting and active identity is retained until cleanup drains, then clears to idle. Hardware proof is still required for physical active/preheat abort motion. |
| `rootfs/home/cygnus/marlindriver/print_service.py` | Completion callback clears the active service file/type after the executor's final queue-completed callback. | Native finish cleanup now keeps status active until the finish policy/drain completes, then marks complete and clears active file/source/UUID/heater targets with host tests. Bounded Z-only hardware completion proof now exists; representative Cura/slicer completion remains part of broader client parity. |
| `rootfs/home/cygnus/marlindriver/marlin_executor.py` | Pause saves position with `M114`, retracts/wipes/parks/cools, waits with `M400`, then resumes through reheat, XY/Z restore, extrusion restore, `G10`, `G92`, and `M105`. | Native pause/resume policy can claim stock-derived design where host-tested. Active pause now requests and drains `M114` before saving the restore position and starting cleanup, matching the stock callback ordering. A supervised bounded native active-print pause/resume/abort smoke passed on June 9, 2026 with all-axis prehome, absolute-mode Z load, correct printing/paused/resumed/aborting/idle status transitions, and Stop allowed only while active/paused. Broader parity still needs Cura-started and representative slicer geometry proof. |
| `rootfs/home/cygnus/marlindriver/marlin_datalink.py` | Keeps pending packets, sends raw bytes, handles resend by sending two `0xff` bytes then replaying pending packets, clears resend state on acknowledge, and ignores nested rejects while resending. | Native flow-control behavior can be claimed where host tests cover ACK/reject/resend and repeated-reject failure handling. Active-print desync behavior still needs live proof because it affects physical job safety. |

## Current No-Overclaim Rules

- A parser recognizing a Marlin line is not evidence that native runtime requests that line on the physical printer.
- A smoke harness or verifier existing is not evidence that the corresponding live phase has passed.
- A static no-Python package audit is not evidence that every client stopped depending on old Python-shaped bridge state.
- A stock-compatible default such as `firmware:"none"` is not a failure by itself; compare stock and native under the same firmware state before treating it as a defect.
- Firmware/version parity must use a paired stock/native `firmware-proof`
  summary. The comparer rejects native `firmware=none` when stock reported a
  real value and rejects missing or zero ambient bed/nozzle telemetry.
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
- June 9, 2026 observe-only client proof:
  `dist/Deneb_Update_fa29a67.deneb` installed over SSH, rebooted, and the
  installed `/usr/bin/deneb-printsvc-smoke --native --boot-sync --client-proof`
  summary passed `/usr/bin/deneb-printsvc-smoke-verify --native --idle
  --boot-sync --client-proof`. It proved native-only route, boot-sync readiness,
  UM API `/printer/status`, `/printer`, and `/system`, Cura cluster
  `/printers`, `/print_jobs`, and `/materials`, installed Digital Factory bridge
  status, no stock `print_service.py`, and final process samples for
  `/usr/bin/deneb-printsvc`, `/usr/bin/deneb-api`, and `/usr/bin/deneb-ui`. The
  optional UM `/print_job` probe returned rc 8 and is recorded as optional
  evidence, not as a required client contract.
- The same smoke run proved temperature reporting was alive at idle: bed current
  was about 25.5 C with target 0.0 C, and nozzle current was about 28.3 C with
  target 0.0 C. This closes the observe-only `0/0 C` regression check, not any
  physical heating phase.
- The client-proof run also showed nonzero ambient temperatures on the native
  route: bed current about 30.3 C with target 0.0 C and nozzle current about
  32.8 C with target 0.0 C.
- The smoke harness process sampler now records executable-level samples so the
  smoke wrapper shell is not counted as `deneb-printsvc` RSS evidence, and the
  verifier rejects literal `print_service.py` process samples without matching
  `print_service_py` summary fields.
- The smoke harness now has observe-only `--firmware-proof` evidence. It
  records `/printer`, bed-temperature, hotend-temperature, and Air Manager
  status fields into scalar summary keys for firmware, machine/PCB metadata,
  ambient bed/nozzle temperatures, topcap telemetry, and status. This closes
  the harness gap for firmware/version parity collection only; live stock and
  native runs still need to be captured on the same firmware before the
  Section 8 live parity item is checked.
- June 9, 2026 installed `dist/Deneb_Update_34518e8.deneb` evidence:
  observe-only
  `/usr/bin/deneb-printsvc-smoke --native --boot-sync --client-proof
  --firmware-proof` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --boot-sync
  --client-proof --firmware-proof
  /tmp/deneb-printsvc-firmware-proof.summary`. The accepted summary showed
  native route ownership, no stock `print_service.py`, final `idle` with
  `native_active:false` and `native_stop_allowed:false`, `firmware=none`,
  `machine_type=none`, `pcb_id=0`, `pcb_id_valid=false`, ambient bed/nozzle
  temperatures around 30.1 C / 33.2 C, and topcap present/current around
  30.0 C. This is native-side evidence only; the stock Python side still needs
  a paired capture before firmware/version parity can be checked off.
- Host-side smoke and native-audit selftests now prove full physical summaries
  must include `phase=*-safety kind=physical` records with axes, required
  homing, expected travel/range, and stop conditions. This closes the harness
  safety-audit contract, not the live physical proof.
- The smoke harness now generates fresh bounded Z-only active/completion
  fixtures and a low-temperature preheat-abort fixture. These are safer inputs
  for live evidence collection than stale ad hoc files, but generated fixtures
  still do not prove physical behavior until run under supervision.
- June 9, 2026 installed `dist/Deneb_Update_be6a5b7.deneb` evidence: installed
  package selftests passed, generated `/tmp/deneb-active-z.gcode` and
  `/tmp/deneb-preheat-abort.gcode` on-device, observe-only native
  restart/boot-sync smoke passed, low-temperature preheat-abort smoke passed,
  and bounded Z-only active-abort smoke passed. Both physical abort summaries
  showed active `printing` with `native_active:true` and
  `native_stop_allowed:true`, then final `idle` with `native_active:false` and
  `native_stop_allowed:false`. The active-abort fixture moved Z from 207.0 to
  202.6, away from homed Z max; it did not exercise X/Y print geometry.
- June 9, 2026 installed `dist/Deneb_Update_7f070d7.deneb` evidence: a
  bounded Z-only completion run using
  `/tmp/deneb-complete-ui-label-fix.gcode` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --complete-job`. The
  summary kept the native-only route, no stock `print_service.py`, initial
  idle with native active/stop false, final idle with native active/stop false,
  and a `z_home` physical safety plan. The filtered device log reported both
  `deneb-api: print completed` and `backend: print completed`, with no
  `serial_fault` or flow-control desync line in that check.
- June 9, 2026 installed `dist/Deneb_Update_d0b61f7.deneb` evidence: a
  supervised bounded native pause/resume/abort run used
  `/usr/bin/deneb-printsvc-smoke --physical-ok --native --job
  /tmp/deneb-pause-z.gcode --pause-resume --prehome-action home` and passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --job --pause-resume
  /tmp/deneb-printsvc-smoke-pause-resume-home.summary`. The harness required
  all-axis prehome because pause/resume moves X/Y, generated an absolute-mode
  bounded Z fixture so the test matched Cura-style XYZ positioning, and proved
  `printing` with Stop allowed, `paused` with Stop allowed, resumed
  `printing`, `aborting` with Stop disabled while cleanup drained, then final
  `idle` with `native_active:false` and `native_stop_allowed:false`. The final
  printer state had `has_error:false`, ambient bed/nozzle temperatures, no
  stock `print_service.py`, and no `POSITION_ERROR`, `macro failed`, or
  print-ended-with-error log lines for the accepted run.
- June 9, 2026 installed `dist/Deneb_Update_34518e8.deneb` evidence: a
  supervised Cura cluster upload/start/abort smoke used a generated bounded
  Z-only fixture at `/tmp/deneb-cura-z.gcode` with no heat, no extrusion, and no
  X/Y motion. The cluster multipart upload/start returned success, the running
  snapshots showed `status=printing`, `native_active:true`, and
  `native_stop_allowed:true`, the cluster DELETE abort was accepted, the
  immediate and draining snapshots showed `status=aborting` with Stop disabled,
  and the final state returned to `idle` with `native_active:false`,
  `native_stop_allowed:false`, blank filename, ambient temperatures, and no
  stock `print_service.py`. This closes only the bounded cluster API
  upload/start/abort slice, not representative Cura client or slicer-geometry
  parity.
- June 10, 2026 installed `dist/Deneb_Update_b0fa27b.deneb` evidence:
  installed CLI, init-handoff, release-gate, native-audit, and integration-audit
  selftests passed; observe-only
  `/usr/bin/deneb-printsvc-smoke --native --restart --boot-sync
  --client-proof --firmware-proof` verified native-only ownership, no stock
  `print_service.py`, UM API, Cura cluster, Digital Factory bridge, ambient
  bed/nozzle/topcap telemetry, `firmware=none`, and final idle active/Stop
  flags false. A supervised bounded Z-only completion/resource run using
  `/tmp/deneb-b0fa27b-complete-z.gcode` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --complete-job
  --resources /tmp/deneb-printsvc-b0fa27b-complete.summary`. It required
  `z_home`, moved only bounded negative Z, showed active `printing` with Stop
  allowed, then final `idle` with active/Stop false and blank filename, plus
  native RSS and throughput samples. A post-idle sequence-number resync
  diagnostic remained in the final API snapshot, but the accepted run's device
  log check found no `POSITION_ERROR`, endstop, homing, or fault lines. This
  refreshes bounded native completion evidence only; representative slicer
  geometry and paired stock/native resource comparison remain open.

## Open Parity Work

- `G280`, `M109`, `M190`, raw multi-line `GCODE` wait sequencing, stock-order pause position capture, and abort cleanup-before-idle timing have host-tested native coverage but still need broader hardware proof where they drive physical heat or motion. Pause/resume now has bounded active-print hardware proof, and completion active-identity cleanup has bounded Z-only hardware proof, but neither has representative Cura/slicer geometry proof.
- Prove native client behavior on hardware for coordinator, LCD UI, web/API,
  Cura LAN, and Digital Factory without stock Python fallback or stale
  pending-job state. Current client-proof evidence covers observe-only UM API,
  Cura cluster status/material/job-list endpoints, and Digital Factory bridge
  status, plus a bounded Z-only Cura cluster upload/start/abort run; it does
  not prove LCD/Web UI user flows, representative Cura client/slicer geometry,
  or Digital Factory job lifecycle.
- Use [PRINTSVC_INTEGRATION_AUDIT.md](PRINTSVC_INTEGRATION_AUDIT.md) as the
  owner/removal-condition map for patched stock-driver client boundaries. The
  static integration audit is now gated in source/package/archive/installer
  paths, but it is not live parity evidence by itself.
- Regenerate representative Cura, representative completion, and resource comparison evidence using the new physical safety-plan records. Repeat active/preheat abort and pause/resume with a real representative print before broad parity is claimed; the current physical evidence is bounded Z-only plus low-temperature preheat.
- Keep Section 8 open until every checked live claim points to current hardware evidence, not just host tests or package gates.
