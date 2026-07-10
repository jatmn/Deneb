# Resource Reduction Plan

> **Archived:** This is retained for historical traceability. It is not the
> current project status or work queue. See [../PROJECT_STATUS.md](../PROJECT_STATUS.md).

Deneb assumes the stock firmware is already constrained by RAM, CPU, boot time,
storage, and UI latency. Matching stock is useful during migration, but the
target state is measurably lighter and easier to reason about.

Status reconciliation: 2026-07-09. The process numbers below are dated
measurements, not verified-current state. The June 22 idle snapshot still had
`coordinator.py`; the newer June 28 coordinator-disabled package demonstrated
selected zero-Python workflows but did not include a replacement full resource
matrix and exposed failed/incomplete Pause, material, and leveling workflows.
The printer did not answer SSH during this reconciliation. See
[PROJECT_STATUS.md](../PROJECT_STATUS.md).

## Current Baseline

Evidence accepted so far:

- Stock Python touchscreen baseline: about 33.7 MB VSZ and 21 MB RSS.
- Current Deneb native touchscreen idle sample: about 2.9 MB VSZ-style process
  size on 2026-06-22, with earlier RSS samples around 1.5-2 MB.
- Current Deneb whole-system idle sample on 2026-06-22: `-/+ buffers/cache`
  used/free improved from stock 74,712/49,872 KB to 31,816/92,768 KB.
- The 2026-06-22 live Python inventory showed only stock
  `coordinator.py` remaining: 27,352 KB VSZ, 22,424 KB RSS. Stock
  `executor.py`, `connector.py`, `print_service.py`, and `compile_all` were
  absent from the live Python process set. Source audit shows this remaining
  coordinator process is a startup/fallback policy issue, not the selected Deneb
  LCD/Web/API/Cura print-status route.
- The 2026-06-22 relevant long-running idle stack sample was 35,472 KB across
  `deneb-printsvc`, `coordinator.py`, `deneb-ui`, `deneb-api`, `lighttpd`, and
  `deneb-mdns`, compared with 115,964 KB VSZ for the original stock Python set.
  The June 28 package disabled the coordinator and proved selected zero-Python
  routes, but a new complete idle/load resource capture is still required.
- Deneb UI package lane bundles LVGL, generated i18n fonts, web/API runtime,
  mDNS, native print-service, smoke/audit tools, macros, notices, and manifest
  data.
- Dormant stock touchscreen files are hidden/pruned after native UI smoke
  passes; this reduces live filesystem clutter but does not reclaim read-only
  squashfs space.
- Stock AP/captive-portal setup, AP-side DHCP/DNS, and IPv6 router-advertisement
  services are disabled for Deneb installs while client networking stays active.
- Native print-service bounded comparison currently shows lower driver RSS and
  system memory than stock Python for the accepted physical fixture set.

These numbers are evidence for the measured slices only. They do not prove full
release readiness for Cura, Web UI, local printing, diagnostics export, updates,
or long active prints.

## Release Gates

New stable or non-experimental packages should require:

- Stock/native summaries that pass `deneb-printsvc-smoke-verify` and
  `deneb-printsvc-smoke-compare --require-reduction`.
- Hardware evidence for every promoted workflow, not just static audits.
- CPU, RAM, RSS/private memory, fd count, thread count, tmpfs usage, log growth,
  and boot/readiness evidence for relevant services.
- Valgrind Memcheck, ASan, or equivalent host memory tooling where practical for
  native code paths before relying on hardware-only soak evidence.
- Bounded-memory behavior for uploads, print files, logs, thumbnails, manifests,
  and diagnostics.
- Explicit budgets for any new long-running service.
- No resource regression unless a release is explicitly marked as a temporary
  transition or measurement-only build.

## Native Print-Service Status

The native `deneb-printsvc` replacement is the largest current resource target.
It replaces the stock Python `print_service.py` path in experimental packages
while preserving the stock ZMQ command/status contract for clients.

Current accepted proof, indexed in
[PRINTSVC_EVIDENCE_LEDGER.md](../PRINTSVC_EVIDENCE_LEDGER.md):

- Native route ownership and no stock Python driver process in native packages.
- Package and installer audits that reject Python driver artifacts.
- Observe-only firmware/temperature/topcap parity against stock/native captures.
- Bounded heat/preheat Stop, active abort, pause/resume, generated cluster API
  upload/start/abort, completion, and short repeated-job stability slices.
- Strict bounded stock/native resource comparison showing native memory and CPU
  interval improvements while keeping completion throughput above the accepted
  floor.
- Diagnostics log throttling after a live log-growth issue.
- Host Valgrind and ASan/LSan gates for current native C test coverage.

Open promotion gates:

- LCD hands-on workflow, including the physically failed Pause path.
- Web UI hands-on workflow.
- Broader desktop Cura failure paths beyond the proven Cura 5.13 workflow.
- Broader Digital Factory client/soak coverage beyond the proven representative lifecycle.
- Representative real slicer output.
- Multi-hour active heat/motion/job stability.
- Explanation or elimination of the remaining active-soak RSS/private-memory
  staircase.

The next web resource experiment should compare the current
`lighttpd` + `deneb-api` + `deneb-mdns` stack against direct static serving from
`deneb-api`, then consider merging mDNS only if the measured saving justifies
the reduced failure isolation.

## Current Measurements To Collect Next

Collect these on stock where practical and on Deneb native packages. Treat this
as the full promotion matrix; a narrower native replacement slice can proceed
when the workflows it touches have current inventory/resource evidence.

| Workflow | Evidence needed |
| --- | --- |
| Boot to ready | Time to UI/API readiness, service order, failures/restarts |
| Idle | Process RSS/private memory, CPU, fd count, thread count, sockets, logs |
| Web/API polling | 2026-07-10: three SSE clients plus 120 requests passed with flat API RSS; four SSE clients starved REST and retained seven descriptors until reboot. Fix connection isolation/cleanup, then collect CPU and longer-duration evidence. |
| Cura discovery and monitor | mDNS cost, cluster polling behavior, pending-job state |
| Upload/start | RAM while receiving multipart data, spool writes, cleanup on failure |
| Active print | Driver RSS/private memory, throughput, flow-control debt, CPU interval |
| Pause/resume/abort | Cleanup timing, Stop state, heater targets, serial errors |
| Completion | Finish drain, idle transition, history/state cleanup, throughput |
| Diagnostics export | Archive size, tmpfs usage, redaction, CPU/RAM during export |
| Update/install/rollback | tmpfs/storage headroom, manifest/signature checks, recovery state |
| Long soak | Multi-hour heat/motion/job loop with RSS/private plateau or root cause |

## Measurement Rules

- Keep successful per-iteration logs disabled during soak unless debugging a
  failure; measure tmpfs/log growth beside process RSS.
- Run Valgrind Memcheck on host-buildable native code when the validation
  surface allows it. Treat a clean run as a leak/resource regression screen, not
  as proof of target MIPS memory behavior.
- Treat idle observe-only runs as baseline context, not as active print-service
  stability proof.
- Track `deneb_printsvc_rss_kb`, private memory, `VmSize`, `VmData`, fd count,
  thread count, `/tmp` usage, and diagnostics log size in active runs.
- Separate generated bounded fixtures from real slicer output and desktop Cura
  behavior.
- Preserve rejected trials only when they explain a current guardrail. Do not
  keep every historical attempt in this plan.

## Service Strategy

- Prefer original native/static components for hot paths when they reduce RAM,
  CPU, startup time, or latency.
- Keep stock services where they are still needed and measured to be acceptable.
- Replace Python services only with a clean owner, hardware proof, and rollback
  path.
- Avoid adding heavyweight runtimes, package managers, databases, message buses,
  web frameworks, or always-on pollers unless there is no lighter practical
  option.
- Keep compatibility shims temporary and attach removal criteria in the relevant
  audit doc.

## Current Non-Goals

- Rebuilding or shrinking the vendor read-only root filesystem from this repo.
- Claiming full Cura compatibility from cluster API smoke alone.
- Claiming native print-service stability from host tests or idle sampling
  alone.
- Shipping stable updates without rollback, signing, and restore proof.
