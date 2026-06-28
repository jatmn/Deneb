# Coordinator Parity Completion Plan

Date: 2026-06-22

This plan scopes the remaining coordinator-related work after the source audit
of `rootfs/home/cygnus/coordinator/coordinator.py` and all handler nodes. It is
written as a handoff for someone who has not followed the de-Python work.

## Purpose

Deneb already routes normal print/status clients through native services:

- LCD/UI backend: `ui/src/backend_comm.c`
- Web/API backend: `web/src/backend_zmq.c`
- Native print service: `printsvc/src/`
- Shared native print helpers: `common/print/`
- Digital Factory native connector: `dfsvc/src/main.c`

The remaining task is not to rewrite `coordinator.py` wholesale. The task is to
finish or explicitly retire the remaining coordinator behaviors that are only
partially replaced, then prove the stock coordinator fallback can be disabled by
default without breaking Deneb-owned workflows.

## Hard Implementation Rule

Do not modify, extend, or rely on stock Python to complete this plan. The source
under `rootfs/home/cygnus/` is reference material only unless a task explicitly
says to preserve a backup or rollback copy. New runtime behavior must be written
in Deneb-owned native code, which for this repository means the existing C code
lanes used by `ui/src`, `web/src`, `common/print`, `printsvc/src`, and
`dfsvc/src`, plus shell only for installer/audit glue where shell is already the
local pattern. Adding a Python helper, Python daemon, Python compatibility shim,
or Python fallback is not acceptable for de-Python completion work.

## Explicit Non-Goals

- Do not port the stock firmware update service from
  `rootfs/home/cygnus/coordinator/handlers/firmwareupdatehandling.py`.
  Deneb uses `.deneb` package updates through `ui/src/screens/screen_update.c`
  and `ui/installer/update.sh`; stock UM firmware internet/USB tar extraction,
  stepping-stone version parsing, and `verify_image.sh` progress breadcrumbs are
  out of scope unless a separate product decision revives stock firmware update
  compatibility.
- Do not preserve stock Gershwin IPC as a product requirement. If Deneb has no
  user-facing or API-facing need for a stock breadcrumb/service shape, retire it
  instead of porting it.
- Do not disable `/etc/init.d/coordinator` in a package until the source/package
  audit gate and the workflow proof in this plan pass.

## Source Map

Stock coordinator entry point:

- `rootfs/home/cygnus/coordinator/coordinator.py`

Coordinator nodes and adapters:

- Legacy print proxy: `LegacyPrintCommandForwarder` in `coordinator.py`
- Print service command adapter: `rootfs/home/cygnus/coordinator/companion/printer_service_command.py`
- Print service status adapter: `rootfs/home/cygnus/coordinator/companion/printer_service_status.py`
- Print sequencer: `rootfs/home/cygnus/coordinator/handlers/printhandling.py`
- File validation/extract: `rootfs/home/cygnus/coordinator/handlers/filehandling.py`
- Bed heater: `rootfs/home/cygnus/coordinator/handlers/bedheating.py`
- Nozzle heater: `rootfs/home/cygnus/coordinator/handlers/nozzleheating.py`
- Material workflow: `rootfs/home/cygnus/coordinator/handlers/materialhandling.py`
- Bed leveling workflow: `rootfs/home/cygnus/coordinator/handlers/bedleveling.py`
- Fault auto-abort: `rootfs/home/cygnus/coordinator/handlers/faulthandling.py`
- Digital Factory handler: `rootfs/home/cygnus/coordinator/handlers/digitalfactoryhandling.py`
- Stock firmware update handler: `rootfs/home/cygnus/coordinator/handlers/firmwareupdatehandling.py`

Native Deneb replacement areas:

- Print route selection: `common/print/print_backend_route.*`
- Native UI/backend route: `ui/src/backend_comm.c`
- Native Web/API route: `web/src/backend_zmq.c`
- Native print service: `printsvc/src/`
- Print job file, metadata, thumbnails, and spooling: `common/print/print_job_file.*`
- Pending job metadata/conflict prompt: `common/print/pending_job_file.*`
- Pending job registration: `printsvc/src/pending_job_registration.*`
- Print action rules: `common/print/print_action_rules.*`
- Material profile and loaded material/nozzle settings: `common/print/print_profile.*`
- Material screen/workflow helpers: `ui/src/screens/screen_material.c`, `common/print/material_workflow.*`
- Bed leveling UI/helpers: `ui/src/screens/screen_level.c`, `common/print/buildplate_level.*`
- Temperature control: `ui/src/screens/screen_temp.c`, `web/src/api_printer.c`, `common/print/gcode_command.*`
- Fault/error state from native print service: `printsvc/src/status_parser.c`, `printsvc/src/error_map.*`, `common/print/status_payload.*`, `common/print/printer_status_response.*`
- Digital Factory native connector and bridge: `dfsvc/src/main.c`, `web/src/df_bridge.c`
- Installer coordinator fallback shim: `ui/installer/update.sh`, function `patch_motion_stack_boot_order`

## Workstream 1: Add Coordinator Dependency Audit Gate

### Goal

Make it mechanically hard for Deneb source to regain a hidden dependency on the
stock coordinator proxy or stock Gershwin coordinator services.

### Required Work

1. Extend the existing native audit, preferably `tools/deneb-printsvc-native-audit.sh`, or create a narrowly named companion audit if that file would become too broad.
2. Fail the audit when Deneb-owned C/shell source under these paths contains stock coordinator print-route dependencies:
   - `ui/src`
   - `web/src`
   - `common/print`
   - `printsvc/src`
   - `dfsvc/src`
   - `ui/installer`
3. The audit must reject new use of:
   - `tcp://127.0.0.1:5565`
   - `tcp://127.0.0.1:5566`
   - `PRINTER_PUBSUB`
   - `PRINTER_RPC`
   - `coordinator::print::handling`
   - `coordinator::file::handling`
   - `coordinator::material::handling`
   - `coordinator::bed::leveling`
   - `coordinator::bed::heating`
   - `coordinator::nozzle::heater`
   - `coordinator::error::handler`
4. Allow only documented compatibility labels used by the native Digital Factory contract:
   - `coordinator::digitalfactory::status`
   - `coordinator::digitalfactory::handling`
5. Add fixture coverage to the matching selftest so the audit proves both positive and negative cases.
6. Package the audit change through `ui/build-package.sh` if this is a target-installed gate, and update `ui/installer/update.sh` only if the installed package must carry the new audit.

### Expected Result

A reviewer can run one command and prove Deneb clients are not wired back to the
stock coordinator route.

### Suggested Validation

- `wsl -- bash -lc "cd /mnt/c/temp/Deneb && sh tools/deneb-printsvc-native-audit-selftest.sh"`
- `wsl -- bash -lc "cd /mnt/c/temp/Deneb && sh tools/deneb-printsvc-native-audit.sh --source ."`
- `git diff --check`

## Workstream 2: File Validation And Upload Parity

### Goal

Finish or explicitly retire coordinator file-validation behavior in native Deneb code from
`filehandling.py` for Deneb print upload/start flows.

### Stock Behavior To Account For

Reference: `rootfs/home/cygnus/coordinator/handlers/filehandling.py`

Stock file handling provides:

- `VALIDATE_META_DATA`
- `EXTRACT_GCODE`
- UFP metadata read through `cygnus.util.ufp_format`
- G-code extraction to `menu_settings.GCODE_FILE`
- G-code fallback temperature scan for first roughly 50 non-comment lines
- Material conflict check against loaded material GUID
- Nozzle-size conflict check against loaded nozzle size
- Build volume rejection using UM2C bounds:
  - X: 0 to 223 mm
  - Y: 0 to 220 mm
  - Z: 0 to 205 mm
- Fault mapping for unreadable files, build-volume exceedance, and extraction failures

### Existing Native Coverage

- UFP model extraction: `common/print/print_job_file.c`, `deneb_print_job_file_extract_ufp_model_gcode`
- UFP upload normalization: `web/src/api_print_job.c`
- Metadata extraction from G-code headers: `common/print/print_job_file.c`, `deneb_print_job_file_metadata_load`
- Material/nozzle conflict pending state: `printsvc/src/pending_job_registration.c`, `common/print/pending_job_file.c`
- Touchscreen conflict prompt: `ui/src/screens/screen_print_conflict.c`

### Required Work

1. Add a native build-volume metadata parser if the current G-code/UFP metadata path does not expose min/max model bounds.
2. Add a native build-volume validation helper under `common/print/`, using the same UM2C bounds as stock unless a newer Deneb machine-profile constant already exists.
3. Wire validation into upload/start paths before a job can become pending or start:
   - `web/src/api_print_job.c`
   - `printsvc/src/pending_job_registration.c` if registration remains the right abstraction
   - Digital Factory path in `dfsvc/src/main.c` if it bypasses the Web/API upload path for any file type
4. Preserve existing material/nozzle conflict behavior while adding build-volume failures; do not make build-volume failures user-overridable unless explicitly decided.
5. Add clear API/UI failure responses for unreadable file, failed UFP extraction, unsupported/oversized file, and build-volume exceeded.
6. Add host tests for:
   - UFP with valid metadata
   - UFP/G-code requiring material conflict prompt
   - UFP/G-code requiring nozzle conflict prompt
   - file exceeding build volume
   - unreadable or missing file
   - UFP extraction failure
7. Update docs that describe upload/start behavior:
   - `docs/BACKEND_IPC_PROTOCOL.md`
   - `docs/CURA_INTEGRATION.md`
   - `docs/WEB_UI.md`
   - this plan if scope changes

### 2026-06-27 Upload Validation Non-Physical Checkpoint

The web/API Cura upload path now calls the shared native build-volume path
validator before spool storage, pending-job dedupe, or native registration.
The native Digital Factory connector also validates the downloaded or
UFP-extracted G-code before posting it to the local cluster upload API. That
keeps invalid model bounds from becoming pending metadata and returns a native
validation failure without falling back to stock coordinator file handling.

Non-physical validation completed:

- Host helper coverage accepts plain G-code with no slicer bounds metadata.
- Host helper coverage accepts complete in-volume bounds.
- Host helper coverage rejects out-of-volume bounds.
- Host helper coverage rejects incomplete bounds metadata.
- Host helper coverage rejects unreadable or missing upload paths.
- `deneb-dfsvc` source wiring rejects Digital Factory downloads with invalid,
  incomplete, or out-of-volume bounds before local API upload.
- `deneb-dfsvc` musl build links the shared native print-file validator.

Remaining gate: target proof for Web/Cura/API and Digital Factory upload paths,
including one valid job, one build-volume rejection, and one material/nozzle
conflict job while runtime inventory shows no live `coordinator.py` dependency.

### Expected Result

Deneb upload/start paths have native file validation at least as strict as the
stock coordinator path for Deneb-exposed workflows, or any intentionally retired
stock behavior is documented with a reason.

### Suggested Validation

- Host unit tests for `common/print/print_job_file.*` and pending-job registration.
- API upload/start smoke using representative `.gcode` and `.ufp` files.
- Target test through Cura/local API and Digital Factory remote print for one valid job and one conflict job.

## Workstream 3: Material Workflow Parity

### Goal

Finish the native material load/unload/change workflow in Deneb-owned C code enough that Deneb does
not need stock `coordinator::material::handling`.

### Stock Behavior To Account For

Reference: `rootfs/home/cygnus/coordinator/handlers/materialhandling.py`

Stock material handling provides:

- `PREPARE_UNLOAD`, `PREPARE_LOAD`, `PREPARE_CHANGE`
- `START`, `ADVANCE`, `CANCEL`
- Tracker-based busy/reject behavior
- Load/change/unload state transitions: prepared, busy, finalizing, cancelled, final
- Working-position move: `G28`, then `G0 X105 Y0 F9000`
- Old/new material temperature selection
- Nozzle heat wait with timeout/cancel behavior
- Extrude-until-advance with timeout and `M401` cancel
- Retract and home-release macros
- Cooldown and engine reset on cancellation
- Material status states: available, empty, busy, unknown

### Existing Native Coverage

- UI material screen: `ui/src/screens/screen_material.c`
- Helper status/plans: `common/print/material_workflow.*`
- Material movement G-code formatting: `common/print/gcode_command.*`
- Loaded material profile helpers: `common/print/print_profile.*`
- Native backend command route: `ui/src/backend_comm.c`

### Required Work

1. Decide the product shape of the native material workflow:
   - full stock-style staged wizard, or
   - simpler Deneb-native workflow with documented retired states.
2. Implement a native material workflow state model in `common/print/material_workflow.*` or a new module if the current helper is too small.
3. Add explicit state for prepare/start/advance/cancel or document why a simpler state is sufficient.
4. Port the critical safety behavior:
   - block filament movement while printing unless intentionally allowed for paused-change flow
   - heat to material-appropriate target before extrusion
   - wait for target with timeout
   - stop extrusion on cancel/advance using the existing stop command
   - cooldown on cancel/finalize
5. Add UI controls/states in `ui/src/screens/screen_material.c` for any newly introduced workflow states.
6. Add Web/API support only if there is an exposed Deneb API need. Do not invent an API just to mirror stock Gershwin.
7. Add host tests for the state machine and generated command sequences.
8. Add target validation steps for unload, load, cancel while heating, cancel while moving, and cooldown.

### 2026-06-27 Non-Physical Runtime Wiring Checkpoint

Material workflow state ownership is now wired into the user-facing touchscreen
material workflow path without adding Python or stock coordinator dependencies:

- `common/print/material_workflow.h/c` owns target-sent state, movement state,
  prepare/start/cancel/finalize transitions, and UI display status derivation.
- `ui/src/screens/screen_material.c` uses `deneb_material_workflow_t` for the
  load/change screen instead of local `workflow_moving`, `workflow_target_temp`,
  and `workflow_target_sent` state.
- Load and unload buttons still send the existing native G-code command
  sequences, but successful sends now transition through the shared workflow
  state model.
- Host test coverage exercises slider-style unsent target changes, sent target
  heating state, unload movement begin/complete transitions, and status derived
  from the workflow object.

Non-physical validation completed:

- `deneb-printsvc-tests` host binary passed.
- MIPS `deneb-ui` target build passed.

Remaining gate: user-supervised target proof for unload/load/cancel/cooldown
with runtime inventory before, during, and after the workflow.
### Expected Result

Deneb has a native material workflow with clear state ownership and no reliance
on stock `coordinator::material::handling` for exposed UI workflows.

### Suggested Validation

- Host tests for material workflow state transitions.
- Touchscreen test on hardware:
  - unload from loaded state
  - load from empty state
  - cancel before heating completes
  - cancel during extrusion/movement
  - confirm nozzle cooldown after cancel/finalize
- Runtime inventory during material workflows must show no `coordinator.py`, `menu/executor.py`, or stock material navigator process required by Deneb UI.

## Workstream 4: Bed Leveling Workflow Parity

### Goal

Finish or explicitly simplify the native bed-leveling workflow in Deneb-owned C code so Deneb does not
need stock `coordinator::bed::leveling`.

### Stock Behavior To Account For

Reference: `rootfs/home/cygnus/coordinator/handlers/bedleveling.py`

Stock bed leveling provides:

- `PREPARE`, `START`, `ADVANCE`, `CANCEL`
- Tracker-based sequence ownership
- Progress states: none, prepared, moving, at-target, final, cancelled
- Ordered macro execution:
  - `buildplate_level_step1.gcode`
  - `buildplate_level_step2.gcode`
  - `buildplate_level_step3.gcode`
  - `buildplate_level_step4.gcode`
  - `buildplate_level_finish.gcode`
- Wait for motion completion after each macro before allowing the next advance

### Existing Native Coverage

- UI screen: `ui/src/screens/screen_level.c`
- Helper macro mapping: `common/print/buildplate_level.*`
- Native macro execution: `printsvc/src/macro_control.c`
- Packaged macros: `ui/build-package.sh`, `/etc/deneb/marlindriver/gcode/`

### Required Work

1. Decide whether Deneb should keep direct step buttons or adopt a guided prepare/start/advance workflow.
2. If guided, add a native state machine for bed leveling under `common/print/buildplate_level.*` or a new module.
3. Ensure the native service or UI waits until the macro/motion completes before enabling the next step.
4. Add cancel handling that leaves the printer in a known safe state.
5. Add UI progress/status copy for moving, ready for next adjustment, cancelled, and finished.
6. Add host tests for macro order and state transitions.
7. Add hardware validation for full bed-leveling sequence, cancel during sequence, and repeated sequence after cancel.

### 2026-06-27 Non-Physical Runtime Wiring Checkpoint

Bed-leveling workflow state ownership is now wired into the touchscreen
maintenance leveling path without adding Python or stock coordinator
dependencies:

- `common/print/buildplate_level.h/c` owns ordered step progression,
  wait-for-completion transitions, cancel, final, and repeat-after-cancel state.
- `ui/src/screens/screen_level.c` uses `deneb_buildplate_level_workflow_t`
  instead of direct always-available macro buttons.
- The touchscreen now exposes a state-driven next-step action plus cancel, and
  holds the workflow in moving state until the local macro wait completes before
  enabling the next step.
- Host test coverage exercises the full ordered step1 -> step2 -> step3 ->
  step4 -> finish sequence, prevents skipping ahead, blocks advancing while
  moving, and covers cancel/reprepare behavior.

Non-physical validation completed:

- `deneb-printsvc-tests` host binary passed.
- MIPS `deneb-ui` target build passed.

Remaining gate: user-supervised target proof for the full leveling sequence,
cancel during sequence, and repeat after cancel with runtime inventory before,
during, and after the workflow.
### Expected Result

Deneb bed leveling is either documented as intentionally direct/manual, or it
has native guided parity with the stock coordinator sequence. In both cases,
Deneb must not rely on stock bed-leveling coordinator nodes.

### Suggested Validation

- Host tests for macro plan/state machine.
- Touchscreen hardware run through all five leveling macros.
- Runtime inventory during leveling must show no stock menu/coordinator process requirement.

## Workstream 5: Fault Handling And Auto-Abort Policy

### Goal

Replace or intentionally retire the coordinator-wide `FaultBreadcrumb` auto-abort in native Deneb code
behavior with a native Deneb fault policy.

### Stock Behavior To Account For

References:

- `rootfs/home/cygnus/coordinator/companion/printer_service_status.py`
- `rootfs/home/cygnus/coordinator/handlers/faulthandling.py`
- `rootfs/home/cygnus/marshal/types/fault.py`

Stock behavior:

- Parses `received_faults` from print-service status.
- Converts Marlin faults to `FaultBreadcrumb`.
- Fault handler watches breadcrumbs.
- If a print tracker is active, it calls `coordinator::print::handling` with `ABORT` until accepted.

### Existing Native Coverage

- Native status parser: `printsvc/src/status_parser.c`
- Native error mapping: `printsvc/src/error_map.*`
- Native job lifecycle error state: `printsvc/src/job_lifecycle.c`
- API/UI error fields: `common/print/status_payload.*`, `common/print/printer_status_response.*`, `web/src/backend_zmq.c`, `ui/src/backend_comm.c`
- Abort command route: `printsvc/src/job_control.c`, `common/print/print_action_rules.*`

### Required Work

1. Define the Deneb fault policy in docs before coding:
   - Which native faults immediately abort active print?
   - Which faults only display an error?
   - Which faults are recoverable?
2. Add native policy code in `printsvc/src/` if auto-abort belongs in the print service.
3. If UI/API must own some decisions, add a shared policy helper in `common/print/` and wire both UI/API consistently.
4. Preserve or improve user-visible error reporting in:
   - touchscreen status screen
   - Web/API printer status response
   - diagnostics export/logs
5. Add host tests for each fault category and active/idle states.
6. Add a target-safe validation plan. Do not induce destructive hardware faults casually; prefer simulated parser/status fixtures first, then controlled non-destructive failures.

### Expected Result

Deneb has an explicit native fault policy. Either native `deneb-printsvc` aborts
active prints on fatal faults, or the docs explain why a different Deneb policy
is safer. Stock `FaultBreadcrumb` is no longer needed for Deneb-owned routes.

### Suggested Validation

- Host parser tests using sample status lines with `received_faults` and Marlin error text.
- Host job-control tests proving fatal fault while printing transitions to abort/error as designed.
- API/UI status tests proving error fields render consistently.
- Target log review after normal print, pause/resume, abort, and one controlled non-destructive error scenario.

## Workstream 6: `print_on_buildplate` State Decision

### Goal

Decide whether stock `print_on_buildplate` has a Deneb purpose. If yes, replace
it natively. If no, document retirement and make sure no Deneb code depends on it.

### Stock Behavior To Account For

References:

- `rootfs/home/cygnus/coordinator/handlers/printhandling.py`
- `rootfs/home/cygnus/menu/screens/main_menu_page.py`
- `rootfs/home/cygnus/menu/navigator/menu_navigator.py`

Stock print handling sets:

- `configuration.set_option('print_on_buildplate', 'true')` after print prepare
- `configuration.set_option('print_on_buildplate', 'false')` after finish/cancel

Stock menu uses this to alter user flow around whether a printed object is still
on the buildplate.

### Existing Native Coverage

No Deneb-native reference was found in the audited C source. Deneb status/history
tracks active/completed prints through native print state and history files.

### Required Work

1. Confirm no Deneb C source reads `print_on_buildplate`.
2. Decide product behavior:
   - retire it as stock-menu-only state, or
   - replace it with native print history/object-removal state.
3. If retired, add an audit check that Deneb-owned code does not start depending on it.
4. If replaced, define where the state lives and who clears it:
   - `printsvc/src/job_lifecycle.c`
   - `common/print/print_history.*`
   - UI status/complete screen, if any
5. Add tests for finish, abort, cancel-before-start, and reboot-after-finish if state is retained.

### Expected Result

The team can answer whether `print_on_buildplate` is obsolete or native-owned,
and coordinator is not kept alive just to maintain that flag.

## Workstream 7: Coordinator Fallback Disablement

### Goal

After the above gaps are closed or explicitly retired, stop starting the stock
coordinator fallback by default in Deneb packages while keeping a rollback path.

### Current Installer Behavior

Reference: `ui/installer/update.sh`, function `patch_motion_stack_boot_order`.

Current behavior:

- Backs up stock `/etc/init.d/coordinator` to `/home/deneb/backups/deneb-ui/init/coordinator.orig`.
- Installs a Deneb coordinator shim at `/etc/init.d/coordinator`.
- The shim delegates `start` to the backed-up stock coordinator init when present.

### Required Work

1. Complete Workstream 1 audit gate first.
2. Complete or explicitly retire Workstreams 2 through 6.
3. Change the installer coordinator shim so Deneb packages do not start stock coordinator by default.
4. Preserve rollback:
   - keep the backed-up stock init
   - provide a documented way to restore or manually start stock coordinator for diagnosis
5. Update init selftests so they assert the new policy.
6. Update runtime inventory expectations so a live `coordinator.py` is a regression in Deneb mode, not a retained fallback.
7. Run package build and target install on development hardware.
8. Capture runtime inventory at idle and during each workflow below.

### Required Workflow Proof Before Promotion

- Idle boot-ready state
- Local touchscreen status screen
- Temperature set/cooldown
- Material workflow from Workstream 3
- Bed leveling workflow from Workstream 4
- USB/local G-code print start, pause, resume, stop/abort, finish
- Cura/Web/API upload/start with no conflict
- Cura/Web/API upload/start with material/nozzle conflict
- Digital Factory connected status
- Digital Factory remote print with no conflict
- Digital Factory remote print with conflict and Continue/Cancel
- Diagnostics export
- `.deneb` package update screen discovery/start path
- Reboot after completed print
- Reboot after aborted/cancelled print

### Expected Result

A Deneb package boots and runs the supported UI/API/DF/Cura workflows with no
live `coordinator.py` process. If stock coordinator is started manually, it is a
documented rollback/debug action, not a normal Deneb dependency.


### 2026-06-27 Coordinator-Disabled Target Checkpoint

Package `Deneb_Update_bc4e01d8.deneb` was built from the dirty workspace and installed on the live printer at `10.10.10.241`. The packaged `update.sh` replaced `/etc/init.d/coordinator` with the Deneb disabled shim. After reboot, boot logs showed:

```text
deneb-coordinator: stock coordinator disabled by Deneb; use backup to restore
deneb-ui: starting (lang=en) version=bc4e01d8-dirty
deneb-ui: entered main loop
deneb-api: starting (version=bc4e01d8-dirty)
```

Post-reboot API/runtime state:

- `/api/v1/printer/status` returned `"idle"`.
- `/cluster-api/v1/print_jobs` returned `[]`.
- Process list contained `deneb-printsvc`, `deneb-ui`, `deneb-api`, and `lighttpd` only for the print/UI/API route.
- Clean `/usr/bin/deneb-runtime-inventory` run reported `Live Python Processes: none`.
- The `/etc/init.d/coordinator` start link still exists, but it now points to the disabled shim; `running rc 0` for that init script means the shim command succeeded, not that `coordinator.py` is live.

Safe no-motion upload proof:

- Posted an out-of-bounds G-code fixture through `POST /api/v1/print_job`.
- API returned `422 Unprocessable Entity` with the native build-volume validation message.
- Printer status stayed `"idle"` and job queue stayed `[]`.
- Follow-up runtime inventory still reported no live Python processes.

This closes the build/install/idle/safe-upload portion of Workstream 7. It does not close the full promotion matrix, which still requires physical material, leveling, print, Cura/Web/API start, Digital Factory remote-print, diagnostics export, update-screen, and reboot-after-job proofs without `coordinator.py`.

### Next Non-Physical Work

These tasks can move the coordinator-removal effort forward without commanding
motion, heaters, extrusion, or a real print:

1. Fix runtime inventory reporting for the disabled coordinator shim.
   `/usr/bin/deneb-runtime-inventory` currently proves there are no live Python
   processes, but the init-script table can still show coordinator `running rc 0`
   because the disabled shim returns success. Update the inventory script to
   detect the `DENEB_COORDINATOR_DISABLED` marker and report that state as
   `disabled shim`, not `running`.
2. Add a host/selftest case for the disabled-shim inventory state so future
   package audits fail if the wording regresses.
3. Re-run local package/native/init audit scripts after the inventory change.
4. Rebuild the experimental package and, if target access is available, install
   it only far enough to prove boot idle/API/Web status and clean no-Python
   inventory. Do not start physical workflows as part of this step.
5. Update this plan and the evidence ledger with the corrected inventory proof.

Expected result: the non-physical evidence clearly says stock coordinator is
disabled and no live Python is present, without implying the old coordinator
daemon is still running.

### 2026-06-27 Runtime Inventory Non-Physical Checkpoint

The runtime inventory helper now detects the Deneb disabled coordinator shim by
its `DENEB_COORDINATOR_DISABLED` marker and reports coordinator as
`disabled shim` instead of `running rc 0`.

Non-physical validation completed:

- Focused host fixture: fake `coordinator` init containing
  `DENEB_COORDINATOR_DISABLED` produced a coordinator service row with
  `disabled shim | disabled shim` and did not report the coordinator row as
  `rc 0 | rc 0`.
- `tools/deneb-printsvc-native-audit.sh --source .` passed and now requires the
  runtime inventory helper plus disabled-shim detection.
- `tools/deneb-printsvc-native-audit-selftest.sh` passed and now covers missing
  runtime inventory, missing disabled-shim detection, and the actual generated
  inventory row.
- `tools/build-update-release.ps1 -ReleaseChannel experimental` passed. The
  final package verification now requires and extracts `deneb-runtime-inventory`,
  and the archive/package audit proves the packaged helper carries disabled-shim
  detection.
- Built package: `dist/Deneb_Update_bc4e01d8.deneb`, version
  `bc4e01d8-dirty`, size `7690240` bytes.

This closes the non-physical inventory-wording task. It does not close the
physical no-coordinator workflow matrix below.

### Physical Tests Still Required

Do not mark Workstream 7 complete until these user-supervised target workflows
are run with no live `coordinator.py` process before, during, and after each
workflow:

- Local touchscreen status/home screen
- Temperature set/cooldown
- Material workflow from Workstream 3
- Bed leveling workflow from Workstream 4
- USB/local G-code print start, pause, resume, stop/abort, finish
- Cura/Web/API upload/start with no conflict
- Cura/Web/API upload/start with material/nozzle conflict
- Digital Factory connected status
- Digital Factory remote print with no conflict
- Digital Factory remote print with conflict and Continue/Cancel
- Diagnostics export
- `.deneb` package update screen discovery/start path
- Reboot after completed print
- Reboot after aborted/cancelled print

For each physical test, capture API status, queue state, process list, runtime
inventory, and relevant `logread` lines. A passing physical workflow is not just
"the printer appeared okay"; it must include evidence that the native Deneb
route handled the workflow without stock Python coordinator processes.

## 2026-06-28 No-Coordinator Physical Test Evidence

Package `Deneb_Update_471cb1fb.deneb` was installed on target `10.10.10.241` with the stock coordinator replaced by the disabled Deneb shim. Every captured runtime inventory sample below reported `coordinator` as `disabled shim` and found no live Python processes.

| Workflow | Result | Evidence / Findings |
|---|---|---|
| Deneb package update from touchscreen | **PASS WITH UX BUGS** | Update installed and rebooted to manifest version `471cb1fb`; post-reboot status was idle and queue `[]`. UX bugs: update screen is taller than the 320x240 panel, the screen looks stuck on updating, it returns home before a delayed reboot, and it should use a full-screen updating splash. |
| Safe rejected upload | **PASS** | Out-of-bounds G-code upload returned HTTP `422` with build-volume limits, status stayed idle, queue stayed `[]`, and native logs recorded build-volume validation failure. |
| Material load/change touchscreen path | **PARTIAL / FAILED UX** | Load Material only opened a temperature/set screen. Heating used native preheat with no Python, but it exposed itself as `printing`, showed a fake `Current print`, and the cancel button was labeled Stop. Stop returned idle and cooled correctly. Actual load/unload motion completion remains unproven. |
| Bed leveling touchscreen path | **FAIL** | Level Step 1 moved to the pre-leveling position with no Python, but Cancel only greyed the button and displayed cancellation. It did not rehome XYZ or return to the first wizard state. |
| LAN/API motion-only upload/start/complete | **PASS** | `deneb-motion-valid-471cb1fb.gcode` uploaded with HTTP `201`, transitioned `PREPARE -> JOB -> Complete`, queue cleared to `[]`, and no live Python appeared. User visually confirmed the motion matched expectations. |
| LAN/API active abort | **PASS WITH API/UX BUGS** | Longer no-heat motion job reached `printing` with `native_stop_allowed=true`. Plain-body `abort` returned HTTP `200`, cleanup homed/parked, queue cleared, and no live Python appeared. Bugs: the first host `curl` stop attempt used an incorrectly escaped JSON payload and was treated as `print`; the accepted plain-body `abort` path worked. UI showed cooling while motion cleanup continued, and startup appeared to home/move-to-front-middle/home again before motion. |
| Touchscreen Pause/Resume/Stop | **FAIL - FIX IN PROGRESS** | Touchscreen Pause sent `PAUSE` and software status changed to paused with no Python, but physical motion continued. Package `1a4a4afe-dirty` deferred pause state handling and fixed one abort-from-paused transition classifier, but the physical retest still proved Deneb flow drain is insufficient: Marlin planner-buffered movement can continue after API/job status says paused and after Deneb reports no inflight commands. A follow-up planner-sync build that inserted `M400` after streamed motion commands was physically tested on 2026-06-28 and also failed: after the user pressed Pause, motion was still running after 30 seconds, so the job was aborted through the API and returned to idle/queue `[]`. The failed per-motion `M400` experiment was backed out. Current native-code mitigation adds a `Pausing` state so API/UI no longer claim `Paused` until pause cleanup completes. The next native change constrains active job streaming to one in-flight G-code command after preparation/startup so Pause/Stop waits behind a bounded Deneb-side backlog; host printsvc tests pass, but physical proof is still required before this row can pass. |

These results do not close Workstream 7. The current native-code fix in progress is the touchscreen/native pause-resume failure: Pause must stop or park motion within a bounded user-safe interval, must not claim fully paused while planner-buffered motion is still executing, Resume must work for no-heat motion jobs and normal heated jobs, and Stop/abort status must stay in a stopping/aborting state until cleanup is complete.
## Final Acceptance Criteria

The coordinator fallback can be disabled by default only when all of these are
true:

- Source audit rejects Deneb dependencies on stock coordinator print ports and non-DF coordinator services.
- File validation behavior is native-owned in Deneb C code or explicitly retired with documented product rationale.
- Material workflow behavior is native-owned in Deneb C code or explicitly simplified with documented product rationale.
- Bed-leveling behavior is native-owned in Deneb C code or explicitly simplified with documented product rationale.
- Fault handling policy is native-owned in Deneb C code and tested.
- `print_on_buildplate` is either native-owned or retired.
- Stock firmware update coordinator logic is documented as out of scope.
- Package install no longer starts stock coordinator by default.
- Runtime inventory on target shows no live `coordinator.py` during required workflow proof.
- Rollback to stock coordinator remains documented and tested enough for development recovery.

## Completion Log

Current status as of 2026-06-22 after the coordinator-parity source audit:

| # | Workstream | Status | Key Changes / Remaining Gate |
|---|-----------|--------|------------------------------|
| 1 | Coordinator Dependency Audit Gate | **COMPLETE** | `tools/deneb-printsvc-native-audit.sh` rejects stock coordinator print ports, non-DF coordinator services, and new `print_on_buildplate` references. Selftest fixtures cover the negative cases. |
| 2 | File Validation And Upload Parity | **PARTIAL** | Native build-volume metadata parsing and validation are wired into `pending_job_registration_prepare()`, `job_control_accept()`, Web/API upload before spool storage, and `deneb-dfsvc` before Digital Factory local upload. Host tests cover complete-bounds acceptance, out-of-bounds rejection, partial-bounds rejection, missing upload path rejection, and clean `JOB` rejection without stale active state; `deneb-dfsvc` musl build proves the DF source link. Remaining gate: prove Web/Cura/API/DF upload paths on target. |
| 3 | Material Workflow Parity | **PARTIAL - PHYSICAL TEST FAILED UX** | Native workflow state is wired into the touchscreen material load/change path through `deneb_material_workflow_t`; host tests and MIPS UI build passed. 2026-06-28 target proof showed native heat/stop/cooldown with no Python, but Load Material only opened a temperature/set screen, exposed preheat as a print, and did not prove load/unload movement completion. |
| 4 | Bed Leveling Workflow Parity | **PARTIAL - PHYSICAL TEST FAILED CANCEL** | Native workflow state is wired into the touchscreen build-plate leveling path through `deneb_buildplate_level_workflow_t`; host tests and MIPS UI build passed. 2026-06-28 target proof showed Step 1 moved without Python, but Cancel did not rehome or reset the wizard. |
| 5 | Fault Handling And Auto-Abort Policy | **COMPLETE FOR ACTIVE PRINTS** | `docs/FAULT_POLICY.md` defines active-print auto-abort policy. `printsvc/src/service.c` aborts active jobs for thermal, endstop, Marlin, and storage faults while leaving serial/command faults recoverable. Idle faults display errors and do not auto-abort. |
| 6 | `print_on_buildplate` State Decision | **COMPLETE** | Retired as stock-menu-only state. Audit check rejects new references in Deneb-owned code. Native print history (`common/print/print_history.*`) tracks active/completed prints instead. |
| 7 | Coordinator Fallback Disablement | **PARTIAL - PHYSICAL MATRIX IN PROGRESS** | `ui/installer/update.sh` writes a disabled coordinator shim with a tested `restore_stock` rollback command. Package `471cb1fb` was installed on target `10.10.10.241` and proved update, idle, safe rejected upload, LAN/API complete, and LAN/API abort paths without live Python. Remaining blockers: touchscreen pause/resume, bed-leveling cancel, material load/unload completion, DF physical reruns, diagnostics export, and reboot-after-job proofs. |

## Recommended Order

1. Add the coordinator dependency audit gate. **(DONE)**
2. Finish file validation/build-volume parity. **(PARTIAL — host ingress, Web/API pre-spool validation, and DF pre-local-upload source proof added; target upload-path proof still required)**
3. Finish fault policy because it affects print safety and all workflows. **(DONE for active-print policy)**
4. Finish material workflow parity. **(PARTIAL — runtime wiring complete; target material workflow proof still required)**
5. Finish bed-leveling parity. **(PARTIAL — runtime wiring complete; target bed-leveling workflow proof still required)**
6. Decide `print_on_buildplate` retirement/replacement. **(DONE — retired)**
7. Change installer fallback policy and run the full no-coordinator target matrix. **(PARTIAL — package installed and idle/safe-upload no-Python proof captured; physical workflow matrix still required)**
