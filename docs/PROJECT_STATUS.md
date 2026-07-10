# Deneb Project Status

Last reconciled: 2026-07-10

This is the only human-maintained dashboard for current project state. For
navigation and documentation rules, see [README.md](README.md). Detailed test
history belongs in [evidence/](evidence/README.md), and future sequencing belongs
in [PLATFORM_MODERNIZATION_ROADMAP.md](PLATFORM_MODERNIZATION_ROADMAP.md).

## Current source and target boundary

- Local branch: `main`, one documentation commit ahead of `origin/main`, with
  additional uncommitted documentation cleanup.
- Source baseline reviewed: `afbea8c`; local documentation commit: `f76b8e5`.
- Printer package observed on 2026-07-10: `1a4a4afe-dirty`.
- Current source was not deployed because this workstation's WSL service fails
  with `Wsl/EnumerateDistros/Service/E_ACCESSDENIED`.
- The 2026-07-10 automation results therefore prove only the older installed
  package. The 2026-06-28 hands-on run remains the newest UI workflow evidence.

## Status vocabulary

| Status | Meaning |
| --- | --- |
| **SOURCE** | Implementation or packaging exists; no runtime claim. |
| **HOST** | Host tests or static/package audits passed; no hardware claim. |
| **TARGET** | The named workflow passed on the printer on a recorded package/date. |
| **FAILED** | Target evidence demonstrated incorrect or unsafe behavior. |
| **BLOCKED** | Work cannot proceed safely until a stated prerequisite is resolved. |
| **PLANNED** | Accepted future work with no completion claim. |

## Objectives at a glance

| Objective | Current state | What prevents completion |
| --- | --- | --- |
| Native touchscreen | **SOURCE + partial TARGET** | Material workflow, leveling Cancel, update UX, diagnostics, and Pause behavior are not release-ready. |
| Native print service | **SOURCE + HOST + partial TARGET; experimental** | Completion and abort have bounded proof. Latest hands-on Pause failed; the source mitigation is not deployed or target-proven. Long active-soak memory behavior remains open. |
| Remove stock coordinator | **SOURCE + partial TARGET** | Selected workflows ran without Python, but material, leveling, Pause/Resume, diagnostics, and broader recovery paths remain incomplete. |
| Fully remove Python from the firmware | **NOT COMPLETE** | The read-only base image still contains Python; bootstrap patching, rollback, and AVR programming/recovery still depend on it. |
| First-class Web UI | **MVP + partial TARGET** | Control recovery, storage/upload UX, security, diagnostics/update UX, accessibility, and connection cleanup remain incomplete. Four SSE clients currently starve REST and leak descriptors until reboot. |
| Current distro and dependencies | **PLANNED** | No reproducible printer-specific current OpenWrt image, device-tree/kernel port, safe test boot, or rollback lane exists yet. |
| Fully Deneb-owned firmware image | **EARLY / PLANNED** | Display/touch, MCU control and recovery, storage, factory data, update, and recovery must be owned and proven on a clean image build. |
| Modern Marlin controller firmware | **RESEARCHED; PORT NOT STARTED** | UltiMaker's old Connect branch is available, but modern Marlin lacks the Connect board/protocol port and no recoverable test build exists. |

## Done and currently supported

These claims are intentionally narrow:

- Deneb update archives have static gates rejecting Python source/runtime
  artifacts in the package.
- Native implementations exist for the touchscreen, print service, Digital
  Factory connector, Web/API path, and selected coordinator-owned print logic.
- A clean 2026-07-10 reboot reached idle with one API process, an empty queue,
  zero heater targets, and no Python executable process.
- Bounded homing, completion, active abort, low-temperature heat, and cluster
  upload/start/abort passed on installed package `1a4a4afe-dirty` without limit,
  endstop, thermal, or controller alerts.
- All five installed native fixture/static self-test suites passed.
- Cura 5.13 discovery and representative local lifecycle paths have older dated
  target evidence; broader failure handling remains open.
- WiFi and Ethernet USB configuration, the native UI, and the lightweight
  vanilla Web frontend are implemented.

These successes do not make the complete firmware, UI, or print service stable.

## In progress

| Priority | Work | Current position | Exit condition |
| ---: | --- | --- | --- |
| 1 | Restore a reproducible WSL build lane | Setup requirements are documented; local WSL enumeration is broken | Fresh environment builds and audits a current MIPS package reproducibly |
| 2 | Make Pause/Resume safe and bounded | 2026-06-28 Pause allowed motion to continue; installed policy also commands Z205, outside the authorized test envelope; `afbea8c` mitigation has host proof only | Current package passes hands-on Pause/Resume/Stop with bounded motion and zero new alerts |
| 3 | Finish no-coordinator workflow parity | Core route works; material, leveling Cancel, diagnostics, and recovery gaps remain | Full workflow matrix passes with strict no-Python runtime inventory |
| 4 | Fix Web/API concurrency and lifecycle ownership | Three SSE clients plus polling passed; four SSE clients starved REST and retained seven descriptors; service restart can create two API processes | Multiple clients, reconnects, service restarts, uploads, and long polling return to baseline without starvation or leaked descriptors |
| 5 | Explain print-service soak memory behavior | Short repeated jobs pass; longer runs showed a resident/private-memory staircase | Multi-hour representative run plateaus or the growth source is fixed |

## Known defects and safety blocks

| Severity | Defect | Current evidence / required response |
| --- | --- | --- |
| **Safety blocker** | Pause can change software state while motion continues | Failed 2026-06-28 hands-on run; do not promote until a current build passes physical proof |
| **Safety blocker** | Installed Pause policy commands Z205 | Outside the authorized 0..190 mm automation envelope; revise policy before automated retest |
| **Release blocker** | Material load/change does not prove a complete load/unload workflow | Fix state, heat presentation, extrusion sequencing, and hands-on completion |
| **Release blocker** | Leveling Cancel did not rehome/reset the wizard | Implement deterministic cancel cleanup and target-test it |
| **Release blocker** | Four SSE clients can starve REST and retain descriptors/sockets | Isolate capacity, close abandoned proxy/API sockets, and add regression coverage |
| **Release blocker** | API init ownership can create duplicate processes | Establish one service owner and test restart/fallback behavior |
| **Incorrect state** | Manual heat reports the printer as printing | Separate heater activity from job activity in shared status classification |
| **Missing product function** | Diagnostics export is not installed | Implement bounded, redacted diagnostics generation/download and resource-test it |
| **Incorrect identity** | Printer API reports missing/invalid firmware, machine, and PCB identity | Populate identity from authoritative device/config sources |
| **Test tooling defect** | Runtime inventory can false-positive on shell command text containing `python` | Classify by `/proc/<pid>/exe` and structured argv instead of substring alone |
| **UX blocker** | Update screen overflowed, appeared stuck, and lacked a dedicated updating state | Redesign and prove update/reboot/rollback presentation |

No unattended physical test may exceed the documented safety contract: home
before motion, stop on any limit/endstop alert, keep commanded X/Y/Z within the
approved test envelope, and obey heater caps.

## Planned sequence after current blockers

1. Build and deploy current source through the documented WSL environment.
2. Close Pause/Stop, material, leveling Cancel, diagnostics, and Web connection
   cleanup on the legacy image.
3. Replace the Python AVR/mainboard programming and recovery utility with a
   native, recoverable implementation.
4. Prototype direct static serving from `deneb-api`; remove lighttpd only after
   resource, malformed-client, upload, SSE, restart, and rollback comparisons.
5. Build a reproducible current OpenWrt image and prove non-destructive boot and
   recovery on spare hardware.
6. Produce a Deneb-owned image without UltiMaker application files while
   preserving required factory data and controller recovery.
7. Port Connect controller support and host protocol behavior onto maintained
   Marlin, with a known-good hex and bootloader recovery available first.

Detailed phase gates are in
[PLATFORM_MODERNIZATION_ROADMAP.md](PLATFORM_MODERNIZATION_ROADMAP.md).

## Evidence pointers

- Latest bounded target run:
  [evidence/TARGET_AUTOMATION_2026-07-10.md](evidence/TARGET_AUTOMATION_2026-07-10.md)
- Current print-service acceptance ledger:
  [PRINTSVC_EVIDENCE_LEDGER.md](PRINTSVC_EVIDENCE_LEDGER.md)
- Current shared-ownership audit:
  [PRINTSVC_INTEGRATION_AUDIT.md](PRINTSVC_INTEGRATION_AUDIT.md)
- Historical stock/platform investigation:
  [evidence/FIRMWARE_AUDIT.md](evidence/FIRMWARE_AUDIT.md)
- Historical resource measurements:
  [evidence/BASELINE_MEASUREMENTS.md](evidence/BASELINE_MEASUREMENTS.md)
- Archived no-coordinator execution journal:
  [archive/COORDINATOR_PARITY_COMPLETION_PLAN.md](archive/COORDINATOR_PARITY_COMPLETION_PLAN.md)

Evidence files prove only their recorded version, date, and workflow. They do
not override this dashboard's current classification without reconciliation.
