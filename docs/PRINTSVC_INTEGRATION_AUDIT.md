# Native Print-Service Integration Audit

This machine-audited specification assigns every retained stock-shaped boundary
to a native owner. It is an ownership contract, not a migration diary. Dated
client and package results belong in
[evidence/PRINTSVC_TARGET_EVIDENCE_HISTORY.md](evidence/PRINTSVC_TARGET_EVIDENCE_HISTORY.md).

| Integration | Placement decision | Native owner | Compatibility boundary | Removal condition | Evidence | Remaining proof |
| --- | --- | --- | --- | --- | --- | --- |
| LCD `backend_comm` | Client via shared helpers. | `common/print` plus native service IPC. | Touchscreen transport/display adapter. | Remove local lifecycle and pending-job interpretation. | Shared route, status, pending-job, formatting, state helpers, and `deneb_status_state_has_abort_context`. | Current LCD lifecycle and recovery matrix. |
| web `backend_zmq` | Client via shared helpers. | `common/print` plus native service IPC. | Web/API transport cache and status adapter. | Remove local lifecycle, completion, and pending-job interpretation. | Shared route, status, pending-job, history, formatting, and state helpers. | Current Web lifecycle and stale-state matrix. |
| REST `api_print_job` | Client via shared helpers. | Shared registration, action, summary, and state helpers. | UM API v1 upload/action adapter. | Remove endpoint-local action branching. | Uses `pending_job_registration`, `print_action_dispatch`, `print_job_summary`, and shared state rules. | Current upload/start/abort/completion target proof. |
| Cura `api_cluster` | Client via shared helpers. | Shared pending-job, profile, summary, and state helpers. | Cura LAN route and conflict adapter. | Keep protocol surface; remove duplicated queue/status interpretation. | Uses shared cluster action planning and has bounded Cura 5.13 target evidence. | Broader Cura versions and failure paths. |
| REST `api_printer` | Client via shared helpers. | Shared motion, command, status-response, and heat-target rules. | Stock-shaped printer-control adapter. | Move every raw command plan to shared/native policy. | Uses `gcode_command`, `manual_motion`, and `printer_status_response`. | Supervised motion/heat endpoint proof. |
| conflict and preheat bridges | Client via shared helpers. | Shared pending-job, action, and state rules. | Separate LCD, REST, and Cura presentation adapters. | Remove bridge-local lifecycle decisions when native fields suffice. | Shared dispatch and state helpers plus native active/stop fields. | Representative client proof. |
| pending-job metadata files | Shared library/API boundary. | Shared pending-job and print-job helpers. | Temporary migration handoff between upload, prompts, and `JOB`. | Replace with one authoritative native state API. | All clients and native registration use shared helpers. | Non-Cura restart recovery. |
| direct macro calls | Native service-owned. | Native macro registry and Deneb-owned defaults. | Clients may retain the stock-shaped `MACRO` frame. | Remove client fallback to stock macro paths. | Native registry, package defaults, and archive audits. | Physical proof for each macro class. |
| direct raw G-code calls | Native service-owned with shared planning helpers. | Shared command/motion helpers plus native rewrite, wait, and dispatch policy. | Clients may retain the stock-shaped `GCODE` frame. | Eliminate unreviewed client string assembly. | Client audits reject raw motion/heater literals. | Physical heat/motion and wait proof. |
| duplicated status classification | Shared library/API boundary. | Shared state, payload, response, and native serialization rules. | Clients render stock-shaped status for their surfaces. | Remove client lifecycle heuristics as native fields become authoritative. | Shared abort, transition, completion, and preheat classification. | LCD/Web transition proof. |
| diagnostics and error mapping | Native service-owned with shared export helpers. | Native diagnostics/error modules plus shared export. | UI, API, smoke, and log presentation remain adapters. | Remove duplicated text mappings covered by service codes. | Modular native diagnostics and bounded export helpers. | Recoverable/fatal fault comparison. |
| native `deneb-printsvc` callers | Native service-owned. | Native serial, framing, flow, lifecycle, job, heat, pause, and diagnostics modules. | Stock-shaped ZMQ frame remains the client contract. | Require no stock fallback plus current hardware/resource evidence. | Modular source, host tests, and package/native audits. | Full target smoke and promotion matrix. |

## Enforcement

The companion `deneb-printsvc-integration-audit` verifies this document and
the source wiring. New client code must declare an owner, compatibility
boundary, removal condition, evidence class, and remaining proof.

The audit also keeps raw motion/heater G-code literals and direct pending-job
paths out of UI and Web/API adapters. Those details remain confined to
shared/native policy owners rather than returning as per-client patches.
