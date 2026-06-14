# Native Print-Service Evidence Ledger

This is the current proof index for the native `deneb-printsvc` milestone. Keep
history in the audit documents only when it explains a live guardrail. The
checklist should cite this file instead of repeating every trial.

## Status Summary

Current status: experimental native print-service packages have strong bounded
evidence, but promotion remains blocked by missing hands-on client workflows and
long active-soak proof.

## Current Proof Table

| Gate | Status | Evidence | Boundary |
| --- | --- | --- | --- |
| Native route owns print backend | Proven for experimental packages | Live summaries with `native_only_route:true`; no stock `print_service.py`; package/init audits | Stock Python remains reference/baseline material. |
| No Python driver artifact in native package | Proven by static gates | `deneb-printsvc-native-audit`, archive audit, installer manifest checks | Static package cleanliness, not runtime behavior. |
| Observe-only firmware/temperature parity | Proven for paired stock/native capture | `/tmp/deneb-stock-d82245c.summary`; `/tmp/deneb-native-d82245c-observe.summary` | Ambient telemetry only, not heat/motion parity. |
| Heat/preheat Stop safety | Proven for bounded native smoke | `/tmp/deneb-native-heat-fixed2.summary`; `/tmp/deneb-cd4724a-preheat55.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` | Low-temperature generated proof. |
| Active abort cleanup | Proven for bounded native/generated paths | `/tmp/deneb-printsvc-smoke-status-fix-active.summary`; `/tmp/deneb-cura-representative-xyz.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` | Shows native `printing` -> `aborting` -> `idle`; does not prove every slicer/client path. |
| Pause/resume | Proven for bounded native representative fixture, 2026-06-13 touchscreen run on package `072edbc`, and 2026-06-14 Cura-local run on package `ff49e86b` | `/tmp/deneb-printsvc-smoke-pause-resume-home.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary`; user-supervised touchscreen target observation: pause during print, Resume reasserted nozzle heat, waited for temperature, returned to position, and continued print; user-supervised Cura-local target observation plus logs: `JOB` -> `Paused`, `Paused` -> `PREPARE` -> `JOB` | Cold-resume target blocker is closed for touchscreen and Cura-local paths. |
| Print-start prepare sequence | Proven for supervised Digital Factory material-mismatch Continue startup on package `6cd72899` | 2026-06-13 user-supervised target observation through the Digital Factory material-mismatch decision path after package `6cd72899`: no double Z home; print started as expected. Host tests cover `job_streamer` prepare ordering. | Native removes the normal-print `M18 Z` release and second `G28 Z`. |
| Generated cluster upload/start/abort | Proven through cluster API; Desktop Cura discovery/upload/mismatch Cancel/Continue/start/completion/pause/resume/cancel proven on package `ff49e86b` | `/tmp/deneb-cura-representative-xyz.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary`; 2026-06-14 user-supervised Cura 5.13 local-network run: discovery label `Ultimaker-2C-test (Deneb UM2C)`, `.ufp` normalized to extracted `.gcode`, material mismatch reached `wait_user_action`, Cancel returned idle with queue `[]`, Continue transitioned `Idle` -> `PREPARE` -> `JOB`, completion transitioned `JOB` -> `Complete` with history `state":"completed"`, later Cura-local pause/resume transitioned `JOB` -> `Paused` -> `PREPARE` -> `JOB`, cancel transitioned `JOB` -> `ABORT` -> `Idle` with status `idle`, queue `[]`, and no pending metadata, and pending mismatch Cancel survived UI/API/print-service restarts before clearing to idle | Cura Continue-after-restart recovery, progress/time reporting, and broader failure modes remain open. |
| Completion flow drain | Proven for representative Digital Factory completion on package `022077b9` and Desktop Cura-local completion on package `ff49e86b` | `/tmp/deneb-native-g280-resource-v5.summary`; `/tmp/deneb-native-g280-api-catchup-v6.summary`; `/tmp/deneb-cd4724a-complete80.summary`; `/tmp/deneb-b745cfd-physical-lifecycle-long.summary`; 2026-06-14 user-supervised DF completion on package `6cd72899` exposed the finish-park bug; 2026-06-14 user-supervised DF completion on package `022077b9` completed with expected end actions; 2026-06-14 user-supervised Cura-local completion on package `ff49e86b` showed `JOB` -> `Complete`, print history `state":"completed"`, queue `[]`, and pending metadata absent | Native finish cleanup after EOF now runs `M400`, relative `G1 Z3`, `G28 X Y`, `G28 Z`, heaters/fan off, `M400`, `M84`, and preserves `Complete` status for API/history. |
| Stock/native bounded throughput | Proven within accepted floor | Stock `/tmp/deneb-precisewait-stock-resource.summary`: 1401 bytes / 29 s / 48 B/s. Native `/tmp/deneb-precisewait-native-resource.summary`: 1401 bytes / 34 s / 41 B/s. | Comparator enforces the current 85% bounded-fixture floor. |
| Native driver RSS reduction | Proven in paired completion evidence | Stock final `print_service.py` RSS 14616 KiB; native final `deneb-printsvc` RSS 1648 KiB | Applies to the accepted bounded fixture set. |
| Strict stock/native resource gate | Proven for current bounded set | `deneb-printsvc-smoke-compare --require-reduction` over the stock/native summaries plus attached lifecycle and stability evidence | Not a substitute for the still-open client and multi-hour gates. |
| Short repeated-job stability | Proven for five bounded Z-only completion jobs | `/tmp/deneb-84376b4-stability-complete5.summary`; `/tmp/deneb-printsvc-stability-16769-{1..5}.summary` | Multi-hour active soak remains open. |
| Active physical soak memory behavior | Investigated, not promotion-complete | `/tmp/deneb-56c2bb5-active-physical-soak-statefix.summary`; `/tmp/deneb-56c2bb5-zmqhwm-active-soak.summary`; `/tmp/deneb-56c2bb5-cadence-cleanup-active-soak.summary`; `/tmp/deneb-56c2bb5-zmqctx-active-soak.summary`; `/tmp/deneb-a6fe410-long-active-soak.summary` | Remaining RSS/private staircase needs explanation or plateau proof. |
| Diagnostics log growth | Mitigated for current package | Live `/var/log/ultimaker/deneb-printsvc.log` checks after dirty `56c2bb5` rebuild | Continue tracking log size during active soaks. |
| Host native memory tooling | Proven for current unit/selftest surface | `tools/deneb-printsvc-valgrind.sh`; WSL Valgrind 3.24.0; GCC ASan/LSan | Host clean runs do not prove live MIPS long-soak behavior. |

## Latest Package/Runtime Notes

- Latest documented native deployment line: dirty `56c2bb5` experimental
  package, with later active-soak evidence from the `a6fe410` run family.
- Package lane includes `deneb-printsvc`, smoke verifier/comparator, stability
  runner, active physical soak runner, guarded stock-baseline collector,
  native/integration audits and selftests, CLI/init/release-gate selftests,
  Deneb-owned macros, manifest, and notices.
- Installer and release wrapper require native print-service manifest fields
  and reject packages that omit the native evidence gate or include Python
  driver artifacts.
- `ui/build-package.sh` and `tools/build-update-release.ps1` default native
  print-service packages to `experimental`; non-experimental channels require
  verified stock/native summaries and strict resource comparison.

## Active-Soak Finding

The active physical soak series improved the native resident baseline through
fixed-buffer streaming, bounded/conflated ZMQ queues, publish-cadence cleanup,
diagnostics throttling, ZMQ context tuning, and IPC cleanup. The final recorded
long active run completed 20 verified cycles before manual stop during cycle 21.
Settled RSS/private samples still climbed from roughly 1004/328 KiB initially
to about 1060/384 KiB by cycles 19-20, with flat `VmSize`, `VmData`, thread
count, and settled fd count.

Treat this as unresolved resident-page/private-memory growth until a longer
active run proves plateau behavior or identifies a source.

## Open Promotion Gates

Section 8 remains experimental until all of these pass on representative
hardware:

- LCD hands-on queued/start/pause/resume/abort/completion/stale-state workflow.
- Web UI hands-on status/control workflow.
- Desktop Cura discovery, upload/start, monitor, and pending-job behavior.
  Discovery, mismatch prompt, mismatch Cancel, mismatch Continue/start,
  completion, pause/resume, cancel/abort back to idle, and pending mismatch
  Cancel after UI/API/print-service restarts are covered by the 2026-06-14
  supervised Cura 5.13 local-network run on package `ff49e86b`;
  Continue-after-restart recovery and progress/time reporting remain open.
- Digital Factory job lifecycle behavior, not only bridge status reachability.
  Material-mismatch Cancel, Continue/start, Pause, Resume, Stop, and no-double-Z
  startup are covered by the current supervised route. Completion with expected
  end actions is covered on package `022077b9`.
- Representative real slicer output for completion, pause/resume, and abort is
  covered for the tested Cura-local job on package `ff49e86b`; broader slicer
  geometry/failure-mode coverage remains open.
- Stock-parity review for print progress/time reporting: the 2026-06-14
  Cura-local completion stayed at `progress:0.0`, `time_total:0`, and
  `time_elapsed:0` for the whole print and in history.
- Multi-hour active heat/motion/job stability with acceptable memory, tmpfs, and
  diagnostics-log behavior.

## Evidence Hygiene

- Generated fixtures, cluster API smoke, desktop Cura behavior, and arbitrary
  slicer output are separate evidence classes.
- Static audits prove packaging/source guardrails; they do not prove physical
  safety.
- Host Valgrind/ASan prove host test coverage; they do not replace target `/proc`
  and hardware evidence.
- New host-buildable native validation should include Valgrind Memcheck where
  practical, especially after changes to streaming, parsers, lifecycle state,
  or shared print-control helpers.
- Rejected trials should be summarized only when they explain a current
  verifier, comparator, safety gate, or release block.
