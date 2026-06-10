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
| Print throughput versus stock Python | Proven within strict physical-fixture floor | Stock `/tmp/deneb-precisewait-stock-resource.summary`: 1401 bytes / 29 s / 48 B/s. Native `/tmp/deneb-precisewait-native-resource.summary`: 1401 bytes / 34 s / 41 B/s. | The comparator now enforces an 85% floor for bounded physical completion throughput while still rejecting zero throughput and larger regressions. Native remains active through its finish barrier and drains with `flow_inflight=0` / `flow_resend=0`. |
| Native driver RSS reduction | Proven in current paired completion evidence | Stock final `print_service.py` RSS 14616 KiB; native final `deneb-printsvc` RSS 1648 KiB | The paired resource run also improved system memory and CPU interval. |
| Full strict stock/native resource release gate | Proven for current bounded physical evidence set | `/usr/bin/deneb-printsvc-smoke-compare --require-reduction /tmp/deneb-precisewait-stock-resource.summary /tmp/deneb-precisewait-native-resource.summary /tmp/deneb-final-native-full.summary /tmp/deneb-b745cfd-physical-lifecycle-long.summary /tmp/deneb-84376b4-stability-complete5.summary` | Passed with native memory, driver RSS, and CPU interval lower than stock, boot-sync not slower, throughput above the 85% floor, and attached heat/motion/macro/local-job/lifecycle/stability evidence. |
| LCD hands-on workflow | Open | None accepted yet | Needs real touchscreen queued job, start, pause/resume, abort, completion, stale-state recovery. |
| Web UI hands-on workflow | Open | None accepted yet | API proofs exist; browser/user workflow proof remains separate. |
| Digital Factory job lifecycle | Open | Observe-only bridge status only | Needs lifecycle behavior, not just bridge status endpoint reachability. |
| Representative real slicer output | Open | Generated representative fixture only | Needs broader slicer geometry beyond generated bounded fixtures. |
| Repeated-job stability/leak behavior | Proven for short bounded native completion loop | `/tmp/deneb-84376b4-stability-complete5.summary` plus iteration summaries `/tmp/deneb-printsvc-stability-16769-{1..5}.summary` | Five supervised Z-only completion jobs ran through the same native process with guarded Z-home before each job, `rss_delta_kb=0`, final idle, heater targets cleared, no jobs, and no flow resend/reject debt. Multi-hour soak remains a separate open promotion gate. |
| Multi-hour stability/leak behavior | Open | None accepted yet | Needs a longer native uptime/resource soak after the repeated-job loop. |

## Latest Deployed Native Build

- Build: dirty `4166d26` experimental package.
- Package: `dist/Deneb_Update_4166d26.deneb`.
- Device install: completed over SSH; target logs show `deneb-api: starting
  (version=4166d26-dirty)` and native package selftests passed, including
  `deneb-printsvc stability selftest passed`.
- Current stability proof:
  `/tmp/deneb-8e72da5-observe-stability.summary` ran the installed
  `deneb-printsvc-stability` harness in observe-only mode for two short
  samples. It recorded native `deneb-printsvc` RSS at 1152 KiB initially and
  finally, `rss_delta_kb=0`, and `phase=stability-result ... rc=0`. This proves
  the target-side stability harness is installed and functional.
  `/tmp/deneb-84376b4-stability-complete5.summary` then ran five supervised
  bounded Z-only completion jobs through the same native process, with per-run
  summaries `/tmp/deneb-printsvc-stability-16769-{1..5}.summary`. Each
  iteration pre-homed Z to 207.0, completed with final Z 191.0, recorded
  `flow_inflight=0` and `flow_resend=0`, and the aggregate result ended with
  `rss_initial_kb=1164`, `rss_final_kb=1164`, `rss_delta_kb=0`, and `rc=0`.
  The post-run printer state was idle with no queued jobs, heater targets zero,
  native active/Stop false, and no flow resend/reject debt. This closes the
  short repeated-job stability slice, not the multi-hour soak gate.
- Current physical lifecycle proof:
  `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` was collected on the
  previously installed `8e72da5-dirty` runtime with longer bounded fixtures and
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
  `/tmp/deneb-precisewait-stock-resource.summary` and
  `/tmp/deneb-precisewait-native-resource.summary` provide the paired resource
  baseline. The split comparison attaches
  `/tmp/deneb-final-native-full.summary`,
  `/tmp/deneb-b745cfd-physical-lifecycle-long.summary`, and
  `/tmp/deneb-84376b4-stability-complete5.summary` for workflow evidence.
  `deneb-printsvc-smoke-compare --require-reduction` passed with memory
  `101168 -> 93780 KiB` initially and `101476 -> 94108 KiB` finally, driver RSS
  `14504 -> 1576 KiB` initially and `14616 -> 1648 KiB` finally, CPU interval
  `5451 -> 5342` jiffies, boot-sync `0 -> 0` seconds, and bounded completion
  throughput `48 -> 41 B/s`, which is above the comparator's 85% floor.

## Current Promotion Boundary

Section 8 remains experimental until all of these are captured and pass:

- Full native smoke matrix on the current build. Current physical evidence
  verifies generated heat/preheat, pause/resume, active abort, Cura-cluster
  representative XYZ, completion, and final idle cleanup slices; desktop Cura,
  LCD/Web UI, Digital Factory lifecycle, and arbitrary slicer-output workflows
  remain separate gates.
- LCD and Web UI hands-on workflow proof.
- Desktop Cura client proof.
- Digital Factory lifecycle proof.
- Representative slicer-output completion, pause/resume, and abort proof.
- Multi-hour stability/resource evidence beyond the short repeated native
  completion loop.
