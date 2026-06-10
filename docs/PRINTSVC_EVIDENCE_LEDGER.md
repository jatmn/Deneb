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
| Heat and preheat Stop safety | Proven for generated low-temperature native smoke | `/tmp/deneb-native-heat-fixed2.summary`; preheat-abort smoke verifier paths | Bed/nozzle low targets reached, active state was stoppable, cooldown returned idle. |
| Active abort cleanup state | Proven for bounded native/generated representative paths | `/tmp/deneb-printsvc-smoke-status-fix-active.summary`; `/tmp/deneb-cura-representative-xyz.summary`; representative Stop-action smoke | Native intentionally avoids stock Python's unsafe XY/Z abort homing cleanup. |
| Pause/resume | Proven for bounded native generated fixture | `/tmp/deneb-printsvc-smoke-pause-resume-home.summary` | Cura-started representative pause/resume remains open. |
| Cura cluster upload/start/abort | Proven for generated representative XYZ fixture through cluster API | `/tmp/deneb-cura-representative-xyz.summary` | Desktop Cura client behavior and arbitrary slicer output remain open. |
| Completion flow drain | Proven for bounded native completion | `/tmp/deneb-native-g280-resource-v5.summary`; `/tmp/deneb-native-g280-api-catchup-v6.summary` | Native completion ends idle with `flow_inflight=0` and `flow_resend=0`. |
| Print throughput versus stock Python | Narrow bounded fixture is stock-matched | Stock `/tmp/deneb-stock-g280-resource-v3.summary`: 7001 bytes / 195 s / 35 B/s. Native `/tmp/deneb-native-g280-resource-v5.summary`: 7001 bytes / 196 s / 35 B/s. | This fixes the known native throughput regression without widening the unsafe Marlin window. |
| Native driver RSS reduction | Proven in narrow bounded fixture | Stock v3 final `print_service.py` RSS 15552 KiB; native v5 final `deneb-printsvc` RSS 1668 KiB | System-wide memory still needs a clean paired reboot baseline before non-experimental promotion. |
| Full strict stock/native resource release gate | Open | `deneb-printsvc-smoke-compare --require-reduction` | Latest useful comparison was narrow completion/resource evidence, not the full release matrix, and system memory samples were not clean paired reboot samples. |
| LCD hands-on workflow | Open | None accepted yet | Needs real touchscreen queued job, start, pause/resume, abort, completion, stale-state recovery. |
| Web UI hands-on workflow | Open | None accepted yet | API proofs exist; browser/user workflow proof remains separate. |
| Digital Factory job lifecycle | Open | Observe-only bridge status only | Needs lifecycle behavior, not just bridge status endpoint reachability. |
| Representative real slicer output | Open | Generated representative fixture only | Needs broader slicer geometry beyond generated bounded fixtures. |
| Long-duration stability/leak behavior | Open | None accepted yet | Needs multi-hour or repeated-job native uptime/resource evidence. |

## Latest Deployed Native Build

- Build: dirty `cd5eeba` experimental package.
- Package: `dist/Deneb_Update_cd5eeba.deneb`.
- Device install: completed; target logs show `deneb-api: starting
  (version=cd5eeba-dirty)` and native package selftests passed.
- Corrected scheduler proof:
  `/tmp/deneb-native-g280-api-catchup-v6.summary` verified native route
  ownership, active `printing` with Stop allowed, final `idle` with Stop
  disabled, no pending job, idle flow drain, and 1961 bytes in 53 seconds
  at 37 B/s.
- Full bounded completion proof:
  `/tmp/deneb-native-g280-resource-v5.summary` verified native route ownership,
  active `printing` with Stop allowed, final `idle`, `flow_inflight=0`,
  `flow_resend=0`, 7001 bytes in 196 seconds at 35 B/s, and driver RSS around
  1.6 MiB.

## Current Promotion Boundary

Section 8 remains experimental until all of these are captured and pass:

- Full native smoke matrix on the current build.
- Clean paired stock/native resource comparison from a fair reboot baseline.
- LCD and Web UI hands-on workflow proof.
- Desktop Cura client proof.
- Digital Factory lifecycle proof.
- Representative slicer-output completion, pause/resume, and abort proof.
- Long-duration stability/resource evidence.
