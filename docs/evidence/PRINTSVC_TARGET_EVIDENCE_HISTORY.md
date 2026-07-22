# Native Print-Service Target Evidence History

Reconciled: 2026-07-22

This dated evidence summary preserves package-specific print-service results
that would otherwise turn the active evidence ledger into a running diary.
Current status and promotion gates live in
[PROJECT_STATUS.md](../PROJECT_STATUS.md) and
[PRINTSVC_EVIDENCE_LEDGER.md](../PRINTSVC_EVIDENCE_LEDGER.md).

## Package And Workflow Results

| Evidence class | Recorded result | Boundary |
| --- | --- | --- |
| Paired stock/native ambient capture | Stock `/tmp/deneb-stock-d82245c.summary` and native `/tmp/deneb-native-d82245c-observe.summary` established route ownership and observe-only firmware/temperature parity. | Ambient telemetry only; not heat or motion parity. |
| Low-temperature heat and preheat Stop | `/tmp/deneb-native-heat-fixed2.summary`, `/tmp/deneb-cd4724a-preheat55.summary`, and `/tmp/deneb-b745cfd-physical-lifecycle-long.summary` passed their bounded generated paths. | Does not cover arbitrary heat workflows. |
| Active abort | Native/generated runs transitioned `printing` to `aborting` to `idle` and cleared active state. | Does not prove every slicer/client path. |
| Pause/resume | Earlier generated, 2026-06-13 touchscreen, and 2026-06-14 Cura-local runs passed for their packages. The 2026-06-28 no-coordinator touchscreen run failed because software entered Pause while physical motion continued. The 2026-07-10 run did not retry because installed policy commands Z205, outside the authorized 0..190 mm envelope. | The newer failure supersedes any broad promotion claim. Commit `afbea8c` has host-only mitigation proof. |
| Cura 5.13 local workflow | Package `ff49e86b` covered discovery, UFP extraction, material mismatch Cancel and Continue/start, completion, pause/resume, cancel/abort, and pending-state recovery after service restarts. Package `9cdb5d6f` showed reasonable progress and time-left behavior. | Broader Cura versions and failure modes remain open. |
| Web status | Package `9cdb5d6f` exposed live status, progress, time left, temperatures, and filename; a live asset correction made time-left visible. | Web control actions and stale-state recovery were not closed. |
| Digital Factory | Packages `6cd72899` and `022077b9` covered representative material-mismatch decisions, startup behavior, completion, and expected end actions. | Broader client and soak coverage remains open. |
| Short repeated-job stability | `/tmp/deneb-84376b4-stability-complete5.summary` and `/tmp/deneb-printsvc-stability-16769-{1..5}.summary` covered five bounded Z-only completion jobs. | Not a multi-hour active soak. |
| Stock/native throughput | The bounded fixture measured 1401 bytes / 29 s / 48 B/s stock and 1401 bytes / 34 s / 41 B/s native. | Comparator floor is 85% for this bounded fixture. |
| Native print-service memory | Paired completion evidence measured stock `print_service.py` at 14616 KiB RSS and native `deneb-printsvc` at 1648 KiB RSS. | Applies only to the accepted evidence set. |
| SD-card repair | Both partitions were repaired offline and immediate unmounted `e2fsck -f -n` verification returned 0 on 2026-06-27. | Closes the recorded incident, not general storage reliability. |

## July 10 Installed Boundary

The live target on 2026-07-10 ran package `1a4a4afe-dirty`. Bounded
completion, abort, low-temperature heat, cluster upload/start/abort, and native
self-tests passed under the limits in
[TARGET_AUTOMATION_2026-07-10.md](TARGET_AUTOMATION_2026-07-10.md).

The source then under review was not deployed in that run because the
workstation lacked the required WSL build environment. This is historical
execution context, not a current project blocker.

## Active-Soak Investigation

The active physical-soak series introduced fixed-buffer streaming,
bounded/conflated ZMQ queues, publish-cadence cleanup, diagnostics throttling,
ZMQ context tuning, and IPC cleanup. The final recorded long active run
completed 20 verified cycles before manual stop during cycle 21.

Settled RSS/private samples still climbed from roughly 1004/328 KiB initially
to about 1060/384 KiB by cycles 19-20, while `VmSize`, `VmData`, thread
count, and settled descriptor count stayed flat. Treat this as unresolved
resident/private-page growth until a longer active run proves a plateau or
identifies the source.

## Evidence Boundaries

- Generated fixtures, cluster API smoke, desktop Cura, and arbitrary slicer
  output are separate evidence classes.
- Static audits prove source and packaging guardrails, not physical behavior.
- Older successes remain valid only for their named package and workflow.
- A newer failed safety result supersedes any broad promotion inference.
- Target promotion requires the current status matrix and all release gates,
  not the accumulation of historical passes.
