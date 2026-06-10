# Native Print-Service Evidence Ledger

This ledger is the current proof index for Section 8. Keep detailed history in
the audit documents only when it explains a design decision; keep current
promotion status here so the checklist does not become a changelog.

## Current Status

| Gate | Status | Authoritative evidence | Notes |
| --- | --- | --- | --- |
| Native route owns the print backend | Proven for experimental native packages | Release package audits; live native summaries with `native_only_route:true` and no stock `print_service.py` process | Stock Python is still present in the firmware image for baseline comparison, but native packages gate against launching it. |
| No Python driver artifact in native package | Proven by static package gates | `deneb-printsvc-native-audit`, archive audit, installer manifest checks | Python source remains read-only reference material. |
| Firmware/temperature observe-only parity | Proven for paired observe-only stock/native capture | `/tmp/deneb-stock-d82245c.summary`, `/tmp/deneb-native-d82245c-observe.summary` | Proves ambient telemetry/status collection, not physical heat or motion parity. |
| Heat and preheat Stop safety | Proven for generated low-temperature native smoke | `/tmp/deneb-native-heat-fixed2.summary`; `/tmp/deneb-cd4724a-preheat55.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` | Current live proof captured preheat `printing` with bed/nozzle targets 35 C / 55 C, native active true, Stop allowed true, abort-requested `aborting` with Stop disabled, then idle with active/Stop false and heater targets cleared. |
| Active abort cleanup state | Proven for bounded native/generated representative paths | `/tmp/deneb-printsvc-smoke-status-fix-active.summary`; `/tmp/deneb-cura-representative-xyz.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` | Current live proof captured native active-print and Cura-cluster abort transitions through `printing` -> `aborting` -> `idle`, with Stop disabled after abort and no resend/reject debt. Native intentionally avoids stock Python's unsafe XY/Z abort homing cleanup. |
| Pause/resume | Proven for bounded native representative fixture | `/tmp/deneb-printsvc-smoke-pause-resume-home.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` | Current representative REST job proof captured `printing` -> `paused` -> `printing` with native active/Stop flags true before abort. Cura-started pause/resume remains open. |
| Cura cluster upload/start/abort | Proven for generated representative XYZ fixture through cluster API | `/tmp/deneb-cura-representative-xyz.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` | Current cluster proof captured representative XYZ motion via cluster upload/start/abort. Desktop Cura client behavior and arbitrary slicer output remain open. |
| Completion flow drain | Proven for bounded native completion | `/tmp/deneb-native-g280-resource-v5.summary`; `/tmp/deneb-native-g280-api-catchup-v6.summary`; `/tmp/deneb-cd4724a-complete80.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` | Current completion proof captured active `printing`, Stop allowed true, Z movement from 207.0 to 191.0, final `idle`, `flow_inflight=0`, and `flow_resend=0`. |
| Print throughput versus stock Python | Open on current paired evidence | Stock `/tmp/deneb-stock-3c91f5c-complete.summary`: 3641 bytes / 93 s / 39 B/s. Native `/tmp/deneb-native-3c91f5c-prehome200-complete-main.summary`: 3641 bytes / 104 s / 35 B/s. | The native run is safe and drains cleanly, but current strict comparison still rejects throughput. |
| Native driver RSS reduction | Proven in current paired completion evidence | Stock final `print_service.py` RSS 14284 KiB; native final `deneb-printsvc` RSS 1668 KiB | System-wide memory still needs a clean paired reboot baseline before non-experimental promotion. |
| Full strict stock/native resource release gate | Open | `deneb-printsvc-smoke-compare --require-reduction` | Current comparison improves memory and driver RSS but still fails CPU interval and print throughput, so this gate is not closed. |
| LCD hands-on workflow | Open | None accepted yet | Needs real touchscreen queued job, start, pause/resume, abort, completion, stale-state recovery. |
| Web UI hands-on workflow | Open | None accepted yet | API proofs exist; browser/user workflow proof remains separate. |
| Digital Factory job lifecycle | Open | Observe-only bridge status only | Needs lifecycle behavior, not just bridge status endpoint reachability. |
| Representative real slicer output | Open | Generated representative fixture only | Needs broader slicer geometry beyond generated bounded fixtures. |
| Long-duration stability/leak behavior | Open | Stability harness tooling installed and short observe-only sanity run passed | Needs multi-hour or repeated-job native uptime/resource evidence from `deneb-printsvc-stability`; `/tmp/deneb-8e72da5-observe-stability.summary` proves only target-side tool operation and short no-motion sampling. |

## Latest Deployed Native Build

- Build: dirty `8e72da5` experimental package.
- Package: `dist/Deneb_Update_8e72da5.deneb`.
- Device install: completed over SSH; target logs show `deneb-api: starting
  (version=8e72da5-dirty)` and native package selftests passed, including
  `deneb-printsvc stability selftest passed`.
- Current stability-tool proof:
  `/tmp/deneb-8e72da5-observe-stability.summary` ran the installed
  `deneb-printsvc-stability` harness in observe-only mode for two short
  samples. It recorded native `deneb-printsvc` RSS at 1152 KiB initially and
  finally, `rss_delta_kb=0`, and `phase=stability-result ... rc=0`. This proves
  the target-side stability harness is installed and functional; it does not
  close the long-duration/repeated-job stability gate.
- Current physical lifecycle proof:
  `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` was collected on the
  currently installed `8e72da5-dirty` runtime with longer bounded fixtures and
  verified with `/usr/bin/deneb-printsvc-smoke-verify --native --idle --job
  --pause-resume --cura-job --preheat-abort --active-abort --complete-job
  --resources`. It captured representative REST job `printing` -> `paused` ->
  `printing` -> `aborting` -> `idle`, preheat abort `printing` -> `aborting` ->
  `idle` with heater targets cleared, active-abort `printing` -> `aborting` ->
  `idle`, Cura-cluster representative XYZ `printing` -> `aborting` -> `idle`,
  and completion `printing` -> `idle` with Z movement from 207.0 to 191.0.
  Final target status was idle with no pending jobs, native active/Stop false,
  and `flow_resend=0` / `flow_reject=0`.
- Current safety/evidence harness proof:
  `/tmp/deneb-cd4724a-preheat55.summary` verified the generated preheat-abort
  fixture remains observable after prior smoke heat: active snapshot showed
  `printing`, bed/nozzle targets 35 C / 55 C, `native_active:true`, and
  `native_stop_allowed:true`; cleanup settled immediately to `idle` with
  active/Stop false.
- Current bounded completion proof:
  `/tmp/deneb-cd4724a-complete80.summary` verified native route ownership,
  guarded prehome, active `printing` with Stop allowed, final `idle` with Stop
  disabled, Z movement from 207.0 to 191.0, no stock `print_service.py`,
  `flow_inflight=0`, and `flow_resend=0`.
- Current stock/native comparison:
  `/tmp/deneb-native-3c91f5c-prehome200-complete-main.summary` verified native
  route ownership, guarded prehome reaching `z=207.0` before upload, active
  `printing` with Stop allowed, final `idle` with Stop disabled, no stock
  `print_service.py`, `flow_inflight=0`, `flow_resend=0`, and driver RSS around
  1.6 MiB.
  `/tmp/deneb-stock-3c91f5c-complete.summary` and
  `/tmp/deneb-native-3c91f5c-prehome200-complete-main.summary` improve native
  memory and driver RSS, but strict comparison still fails CPU interval
  (`12379` stock vs `13644` native jiffies) and bounded fixture throughput
  (`39` stock vs `35` native B/s).

## Current Promotion Boundary

Section 8 remains experimental until all of these are captured and pass:

- Full native smoke matrix on the current build. Current physical evidence
  verifies generated heat/preheat, pause/resume, active abort, Cura-cluster
  representative XYZ, completion, and final idle cleanup slices; desktop Cura,
  LCD/Web UI, Digital Factory lifecycle, and arbitrary slicer-output workflows
  remain separate gates.
- Clean paired stock/native resource comparison from a fair reboot baseline.
- LCD and Web UI hands-on workflow proof.
- Desktop Cura client proof.
- Digital Factory lifecycle proof.
- Representative slicer-output completion, pause/resume, and abort proof.
- Long-duration stability/resource evidence from the repeated native stability
  harness.
