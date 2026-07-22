# Native Print-Service Evidence Ledger

Reconciled: 2026-07-22

This is the machine-audited current proof index for `deneb-printsvc`. It
records whether each promotion gate has qualifying evidence, not every trial.
Package-specific history lives in
[evidence/PRINTSVC_TARGET_EVIDENCE_HISTORY.md](evidence/PRINTSVC_TARGET_EVIDENCE_HISTORY.md);
project-wide blockers live in [PROJECT_STATUS.md](PROJECT_STATUS.md).

## Current Proof Matrix

| Gate | Current evidence state | Accepted evidence | Boundary |
| --- | --- | --- | --- |
| Native route owns print backend | Proven for experimental packages | Live summaries report `native_only_route:true`; package/init audits reject stock `print_service.py` ownership. | Does not prove every client workflow. |
| No Python driver artifact in native package | Proven by static gates | Native source, package, archive, installer, and manifest audits pass. | Static cleanliness is not target behavior. |
| Observe-only firmware/temperature parity | Proven for paired stock/native capture | `/tmp/deneb-stock-d82245c.summary`; `/tmp/deneb-native-d82245c-observe.summary` | Ambient telemetry only. |
| Heat/preheat Stop safety | Proven for bounded generated paths | Low-temperature heat and preheat-abort summaries recorded in the evidence history. | Not arbitrary heating parity. |
| Active abort cleanup | Proven for bounded native/generated paths | Accepted runs reached `printing` -> `aborting` -> `idle` and cleared native active state. | Not every slicer/client path. |
| Pause/resume | **FAILED in newest hands-on matrix; current fix unproven** | Historical named-package passes exist, but the 2026-06-28 run entered Pause while motion continued; the 2026-07-10 retry was blocked by an out-of-envelope Z205 policy. | Open safety and promotion blocker. |
| Cura local lifecycle | Partial target proof | Cura 5.13 discovery, upload, conflict handling, lifecycle, progress/time, and restart recovery passed on named packages. | Broader versions and failure modes remain open. |
| Web workflow | Partial target proof | Live status, progress, time left, temperatures, and filename passed on a named package. | Controls and stale-state recovery remain open. |
| Short repeated-job stability | Proven for five bounded Z-only completion jobs | `/tmp/deneb-84376b4-stability-complete5.summary`; related five-run summaries are indexed in evidence history. | Not a multi-hour active soak. |
| Active physical soak memory behavior | Investigated, not promotion-complete | Twenty completed cycles were recorded before a manual stop during cycle 21. | RSS/private pages continued to climb; plateau or root cause remains required. |
| Stock/native resource reduction | Proven for the accepted bounded comparison | Native print-service RSS and bounded throughput passed the strict comparator. | Not a full workload/resource matrix. |
| Diagnostics log growth | Mitigated for the tested package | Flow/ACK churn was throttled and the tested log remained bounded. | Continue measuring during active soaks. |

## Installed Target Boundary

The latest recorded target matrix is the 2026-07-10 run on package
`1a4a4afe-dirty`. Completion, abort, bounded low-temperature heat, cluster
upload/start/abort, and installed native self-tests passed. Pause was not
retested under the authorized motion envelope. See
[evidence/TARGET_AUTOMATION_2026-07-10.md](evidence/TARGET_AUTOMATION_2026-07-10.md).

This package boundary is historical target evidence. It does not imply that
newer source is installed or target-proven.

## Promotion Gates

The native print service remains experimental until the current release
blockers in [PROJECT_STATUS.md](PROJECT_STATUS.md) are closed. Its
print-service-specific exit conditions are:

- current-package LCD and Web queued/start/pause/resume/abort/completion and
  stale-state workflows;
- broader Cura and real-slicer failure-path proof;
- full coordinator-disabled client and recovery coverage;
- multi-hour active heat/motion/job stability with bounded memory, storage,
  descriptors, and diagnostics logs;
- strict no-Python runtime inventory for the complete target matrix.

## Evidence Rules

- Static audits prove source/package guardrails, not physical behavior.
- Host tests prove host-native code, not live MIPS behavior.
- Every target claim is bounded to its named package, workflow, date, and safety
  contract.
- Historical successes remain evidence but cannot override a newer failed
  safety result.
- The project dashboard, not this ledger, owns cross-project priority and
  completion status.
