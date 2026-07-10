# Deneb Project Status

Last reconciled: 2026-07-09

Repository: `main` at `afbea8c`

Live-current verification: **unavailable**; the development printer at the
documented address did not answer SSH on 2026-07-09. The newest accepted target
evidence in this repository is dated 2026-06-28.

This is the authoritative project-level status summary. Detailed evidence stays
in the component audits, but a component must not be called complete here merely
because source exists, a host test passes, or an installer contains it.

## Evidence Vocabulary

| Status | Meaning |
|---|---|
| **SOURCE** | Code or packaging exists. No runtime claim is implied. |
| **HOST** | Host tests/static/package gates passed. Hardware behavior is not implied. |
| **TARGET** | A dated, named workflow was observed on the printer with supporting state/process/log evidence. |
| **FAILED** | Target evidence demonstrated a defect or unsafe/incomplete behavior. |
| **STALE** | The statement was once supported but has been superseded by newer evidence. |
| **UNVERIFIED-CURRENT** | No live check was possible during the latest reconciliation. |

Checkboxes elsewhere in the repository apply only to the exact sentence beside
the checkbox. They are not release-readiness indicators.

## Executive Status

| Objective | Honest status | Evidence and blocker |
|---|---|---|
| Native touchscreen | **SOURCE + TARGET, not release-complete** | Native LVGL UI runs with a much smaller measured footprint. The 2026-06-28 no-coordinator matrix still found update-screen UX bugs, incomplete material workflow, failed leveling cancel, and failed pause behavior. |
| Native print service | **SOURCE + HOST + partial TARGET; experimental** | Native route, completion, abort, Cura, and Digital Factory slices have target evidence. The latest physical pause test failed because motion continued after Pause; commit `afbea8c` bounds job streaming to one in-flight command but has host proof only. |
| Native Digital Factory | **SOURCE + partial TARGET** | Pairing, connected state, reconnect, disconnect, rename, remote print, conflict handling, completion, and abort have dated no-`connector.py` evidence. Broader soak and current-live verification remain open. |
| Stock coordinator removal | **SOURCE + partial TARGET; not closed** | The installer now uses a disabled shim and selected 2026-06-28 workflows ran with no live Python. Material, leveling, pause/resume, diagnostics, reboot-after-job, and broader client paths remain incomplete or failed. |
| Fully de-Python firmware | **NOT COMPLETE** | Deneb release archives reject Python, but the stock base image still contains Python, rollback can restore Python coordinator services, the bootstrap installer executes Python to patch stock files, and the AVR/mainboard flasher is Python. Python cannot yet be uninstalled safely. |
| Web UI first-class experience | **MVP / partial TARGET** | Status/progress and Cura-facing APIs have useful target proof. Web pause/resume/cancel recovery, upload failure UX, storage management, diagnostics/update UX, accessibility, authorization/audit, and load/resource measurements remain open. |
| Current distro/dependencies | **NOT STARTED AS A PRODUCT IMAGE** | Stock base is OpenWrt 18.06 snapshot with Linux 4.14.81 and 2018-era packages. Current OpenWrt supports Omega2+, but Deneb has no reproducible OpenWrt image definition, printer-specific device tree/kernel port, or safe boot/recovery lane yet. |
| Independent Deneb firmware image | **FEASIBLE, EARLY** | Most application services now have original native implementations, but the repository has no clean base-image build. Printer display/touch, MCU UART/reset/flashing, storage layout, update/recovery, and factory-data preservation must be ported and proven. |
| Modern Marlin on controller | **RESEARCH COMPLETE ENOUGH TO START A PORT; no build yet** | UltiMaker published an `Ultimaker2+Connect` branch with a 2021 head based on old UltiMaker Marlin. Current Marlin has UM2/UM2+ examples but no E2/2+ Connect board support or UltiMaker host serial protocol. |

## Checklist Accuracy Audit

The checklists are useful implementation inventories, but they have not been a
reliable project status dashboard. The main failure modes were:

1. **Implementation was reported as completion.** Examples include the
   touchscreen replacement and native print service. Both exist and run, but the
   newest physical matrix still contains failed safety/UX workflows.
2. **Older open items were not closed after later evidence.** Digital Factory
   and Cura gained substantial June target evidence while several top-level
   documents still described them as unproven.
3. **Older success text was not reopened after newer failures.** The latest
   touchscreen Pause test supersedes earlier bounded pause/resume proof for the
   broader release claim.
4. **Historical process snapshots were described as current.** The June 22
   snapshot had `coordinator.py` running; the June 28 package deliberately used
   a disabled coordinator shim and demonstrated zero-Python slices. Neither is a
   verified 2026-07-09 live state.
5. **The de-Python scope meant “no Python in a Deneb update archive,” not “Python
   can be removed from the firmware.”** Those are different acceptance gates.

The authoritative ordering is now:

1. This file for project-level truth.
2. `COORDINATOR_PARITY_COMPLETION_PLAN.md` for the latest no-coordinator
   physical matrix.
3. `PRINTSVC_EVIDENCE_LEDGER.md` and `DF_LIFECYCLE_CLASSIFICATION.md` for dated
   component evidence.
4. `UM2C_MODDING_CHECKLIST.md` for task inventory.
5. `FIRMWARE_AUDIT.md` and `BASELINE_MEASUREMENTS.md` for historical platform
   facts and dated measurements.

## De-Python Gap

### What is genuinely complete

- The production `.deneb` package builder rejects `.py`, Python-named files,
  the stock Python Marlin driver, and Python Digital Factory fallback artifacts.
- Native replacements exist for the touchscreen, print service, Digital Factory
  connector, web/API path, and the selected coordinator-owned print workflows.
- Dated target samples show some complete print and cloud workflows with no live
  Python process.

### Why Python cannot be removed yet

| Dependency | Current role | Required replacement/decision |
|---|---|---|
| `packages/ssh-bootstrap/update.sh` embedded Python | Patches stock Cygnus update/menu files during bootstrap | Retire this stock-patching bootstrap for a Deneb image, or replace it with deterministic shell/native transforms for the legacy install lane. |
| `/home/atmel_programmer` Python package | Programs and recovers the AVR motion controller via `/dev/ttyS1`, GPIO reset, bootloader, and software SPI | Build a small native AVR/STK500v2/ISP utility and prove bootloader and application recovery before deleting Python. |
| Stock coordinator rollback | Development recovery path can restore `coordinator.py` | Finish the no-coordinator matrix and replace Python rollback with image/slot rollback or explicitly retain Python in legacy-mod images. |
| Stock Cygnus files and Python runtime in squashfs | Present even when inactive | Rebuild a Deneb-owned OpenWrt image without those packages; overlay whiteouts cannot reclaim or remove the read-only lower layer. |
| Unfinished native workflows | Material load/unload, leveling cancel, bounded Pause/Stop, diagnostics and reboot cases | Fix and target-prove each workflow with runtime inventory showing no Python. |

The extracted stock image contains at least about **43.2 MiB uncompressed** of
Python runtime/library and Python application source by the current local
inventory: 42,116,772 bytes under `/usr/lib/python3.6`, 2,438,855 bytes for
`libpython3.6.so.1.0`, 727,634 bytes of Cygnus `.py`, and 52,626 bytes of AVR
programmer `.py`. This is a lower-bound inventory, not the expected compressed
flash saving.

### De-Python acceptance gate

Do not state that Deneb is fully de-Pythoned until all are true:

- No Python process appears across the full physical workflow matrix.
- No Deneb install, update, recovery, factory-reset, or controller-flash path
  invokes Python.
- A Python-free image boots, prints, updates, rolls back, and recovers the AVR
  controller on spare hardware.
- Image/package audits fail on the interpreter, `libpython`, site-packages,
  `.py`, and `.pyc` outside explicitly desktop-only Cura tooling.
- Removing the Python packages is part of a reproducible image build, not an
  overlay deletion experiment.

## Known Target Failures That Must Stay Visible

From the 2026-06-28 no-coordinator physical matrix:

- Touchscreen Pause changed software state but physical motion continued. The
  newest one-in-flight mitigation is not target-proven.
- Build-plate leveling Cancel did not rehome or reset the wizard.
- Material load/change only reached a heat screen, was presented as a print,
  and did not prove load/unload motion completion.
- The update screen exceeded the display, appeared stuck, returned home before
  reboot, and lacked a dedicated updating state.
- Active abort completed safely in the tested slice, but state/UX and request
  parsing problems were observed.

These findings are release blockers, not cosmetic backlog.

## Web UI Product Gap

The existing static HTML/CSS/JS is appropriately small and does not need a
framework rewrite. The main product gaps are behavior and validation:

- Complete hands-on print lifecycle with stale-state and reconnect recovery.
- Upload progress, validation detail, cancellation, cleanup, storage/free-space
  reporting, history, and local/USB file management.
- Clear safety gating and confirmation for heat, motion, remote control, and
  concurrent operators.
- Authentication lifecycle, logout/session expiry UX, rate limiting, request
  audit, CSRF/origin policy, and a deliberate trusted-LAN posture for Cura
  compatibility routes.
- First-class errors, diagnostics download, update/rollback status, network
  setup/status, printer identity, and actionable degraded/offline states.
- Accessibility, responsive desktop/mobile polish, keyboard use, and browser
  compatibility tests.
- Measured CPU/RSS/socket/log behavior under SSE, polling, upload, print, and
  multiple clients.

### Lighter server direction

The current split is `lighttpd` + `deneb-api` + `deneb-mdns`. The accepted June
snapshot reported process sizes of about 664 KiB for lighttpd, 2,080 KiB for
`deneb-api`, and 252 KiB for `deneb-mdns` (BusyBox `ps` size, not RSS).

The best next experiment is to let `deneb-api` serve the static files directly
on TCP port 80 and remove lighttpd and its Unix-socket proxy. This should save
one process, one packaged dependency, and proxy/socket overhead without adding a
new runtime. Integrating mDNS into the API event loop is a second, smaller
optimization and should be attempted only after measuring the simpler
lighttpd-removal change. Replacing the vanilla frontend with another UI
framework would move in the wrong resource direction.

## Immediate Priority Order

1. Target-prove or revise the one-in-flight Pause/Stop mitigation.
2. Fix material workflow and leveling cancel, then finish the no-coordinator
   matrix with zero-Python inventory evidence.
3. Implement a native AVR programming/recovery utility.
4. Prototype direct static serving in `deneb-api` and measure against the
   current three-process web stack.
5. Establish a reproducible OpenWrt 25.12 image build and non-destructive boot
   laboratory before changing the installed base image.
6. Create and test a modern-Marlin compatibility branch only after preserving a
   known-good controller hex, bootloader recovery, and protocol trace suite.
