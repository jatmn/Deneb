# Low-Hanging De-Python Checklist

Date: 2026-06-11

This checklist captures small tasks that reduce Deneb's active dependency on
stock Python before starting a full native coordinator replacement. The goal is
to de-Python the source base and shipped/runtime surface in controlled steps:
remove Deneb-owned Python helpers from release paths, quarantine reference-only
Python, disable avoidable stock Python startup work, and document which stock
Python dependencies remain because they still live behind required runtime
contracts.

Each item should stay narrowly scoped, preserve rollback behavior, and end with
measurable evidence. Do not claim a Python path is gone from static inspection
alone; prove the package, init, and hardware runtime behavior, or keep the
change guarded.

Related status docs:

- [RESOURCE_REDUCTION_PLAN.md](RESOURCE_REDUCTION_PLAN.md)
- [BASELINE_MEASUREMENTS.md](BASELINE_MEASUREMENTS.md)
- [FIRMWARE_AUDIT.md](FIRMWARE_AUDIT.md)
- [PRINTSVC_EVIDENCE_LEDGER.md](PRINTSVC_EVIDENCE_LEDGER.md)
- [UM2C_MODDING_CHECKLIST.md](../UM2C_MODDING_CHECKLIST.md)

## Ground Rules

- [ ] Keep each task independently shippable and revertible.
- [ ] Avoid copying vendor Python implementation into Deneb native code.
- [ ] Classify every Python path touched by a task as one of:
  Deneb-owned runtime artifact to remove, Deneb-owned reference/tooling to
  quarantine, stock read-only dependency to disable/avoid launching, or stock
  dependency that must remain until a native owner exists.
- [ ] Add a package, installer, or audit check when the task is meant to keep a
  Python path out of Deneb packages.
- [ ] Prefer native C/shared-helper ownership for new runtime behavior. If a
  Python helper remains, document why it is not part of the target runtime.
- [ ] Record before/after process state and resource numbers when a task changes
  boot services or long-running processes.
- [ ] Use Valgrind Memcheck, ASan/LSan, or equivalent host memory tooling for
  host-buildable native code, helper binaries, selftests, and audit fixtures
  touched by these tasks. Treat this as a leak/resource regression screen, not
  as a replacement for target hardware resource measurements.
- [ ] Validate on the target printer before marking a runtime cleanup complete.
- [ ] Update the authoritative status docs only after the evidence exists.
- [ ] Distinguish Deneb package artifacts from stock read-only firmware files.
  For files in the vendor squashfs/rootfs, Deneb can disable services, hide files
  with overlayfs whiteouts, or avoid launching them; it cannot reclaim read-only
  flash space without rebuilding the stock firmware image.

## 1. Remove The Deneb-Owned Python Digital Factory Bridge From Runtime Paths

### Why This Is Low-Hanging

Deneb already has a native C Digital Factory bridge at
`ui/src/df-bridge/deneb-df-bridge.c`. The older Python helper remains at
`ui/scripts/deneb-df-bridge.py` and duplicates the same Gershwin IPC bridge
role. This is not the full Digital Factory cloud connector; it is only the
small UI-side bridge used to request status, pairing, and disconnect actions.
Because this script is Deneb-owned source, it is the clearest first
de-Pythoning task: keep the native bridge as the runtime owner and prevent the
Python bridge from shipping or being treated as a fallback implementation.

### Scope

- [ ] Confirm release packaging installs the native C bridge or symlink and does
  not install `ui/scripts/deneb-df-bridge.py`.
- [ ] Remove, quarantine, or explicitly mark the Python bridge script as
  reference-only in the Deneb repo so it cannot be mistaken for a runtime
  dependency.
- [ ] Add or extend an audit that fails if `deneb-df-bridge.py`, a Python bridge
  launcher, or an unexpected `*.py` UI helper ships in a `.deneb` package.
- [ ] Run Valgrind or sanitizer coverage for any changed native bridge code,
  package-audit helper, or host selftest added for this task.
- [ ] Update UI/Digital Factory docs to state that Deneb uses the native bridge.

### Acceptance Criteria

- [ ] A built `.deneb` package contains no Deneb-owned `deneb-df-bridge.py`
  artifact.
- [ ] `deneb-ui` can still run `status`, `connect`, and `disconnect` Digital
  Factory bridge actions through the native bridge.
- [ ] Package/audit selftests fail on a fixture that includes the Python bridge.
- [ ] Memory-tool runs for changed native/test code are clean or have documented
  expected suppressions.
- [ ] Documentation names the C bridge as the runtime path.

### Suggested Validation

- [ ] Build a release package.
- [ ] Inspect the package file list for `deneb-df-bridge`, `deneb-ui`, and any
  unexpected Python bridge artifacts.
- [ ] On hardware, run the Digital Factory screen status path and confirm it
  returns a compact status line instead of a bridge launch error.
- [ ] Capture process list evidence showing no one-shot Python bridge process is
  left running after the action.

### Risks And Guardrails

- The native bridge intentionally talks to stock coordinator/Digital Factory
  Gershwin IPC. Do not treat this task as replacing the cloud connector.
- This task can remove Deneb-owned Python helper artifacts from Deneb packages,
  but it does not remove stock Python from the vendor read-only rootfs.
- Keep a rollback path: if native bridge behavior regresses, the installer
  should still be able to roll back to a known UI package.

## 2. Make Stock Python UI Pruning Explicit And Auditable

### Why This Is Low-Hanging

The installer already prunes dormant stock Python touchscreen files after the
native UI smoke test. It intentionally keeps shared stock constants such as
`menu_settings.py` and `machine_config.json` because coordinator, file handling,
firmware update handling, host ID, network, and UFP utilities may still import
them. This task makes that boundary explicit and testable.
The de-Pythoning goal is not to pretend the stock read-only image is gone; it is
to make the live Deneb overlay and package behavior depend only on the stock
Python files that are still required by unreplaced stock services.

### Scope

- [ ] Enumerate the exact stock menu files Deneb must retain.
- [ ] Enumerate the stock menu files that should be hidden or overlay-pruned
  from the live filesystem view after Deneb UI smoke passes.
- [ ] Label retained files as "remaining stock Python dependency" and attach the
  stock service or workflow that still needs each one.
- [ ] Add an installer selftest or host fixture that proves the retained files
  are present and the pruned directories are absent.
- [ ] Run Valgrind or sanitizer coverage for any changed native installer-audit
  helper, host fixture, or package selftest that is executable on the host.
- [ ] Add a simple import-dependency check or documented manual check for the
  retained stock Python constants.
- [ ] Update docs to describe the retained shared constants versus removed UI
  implementation.

### Acceptance Criteria

- [ ] The prune list is explicit and documented.
- [ ] The installer keeps only required shared menu configuration files.
- [ ] A Deneb package install does not leave dormant stock screen/navigator/UI
  directories visible in the live overlay filesystem view.
- [ ] Coordinator/update/material-related stock imports still work where Deneb
  has not replaced them.
- [ ] Memory-tool runs for changed native/test code are clean or have documented
  expected suppressions.

### Suggested Validation

- [ ] Run the installer selftest or host fixture.
- [ ] On hardware after install, list `/home/cygnus/menu` and verify retained
  files and removed UI directories.
- [ ] Run the native UI smoke path.
- [ ] Exercise update entry, material import/set material, diagnostics export,
  and any remaining coordinator-backed path that might import stock menu
  settings.

### Risks And Guardrails

- This is cleanup, not a storage-reclaim project. Overlayfs whiteouts can hide
  stock files from the live filesystem view, but they do not shrink or erase the
  vendor read-only squashfs.
- Keep the prune step after native UI smoke so a broken package does not remove
  the stock fallback before Deneb proves it can start.
- Avoid deleting files solely because they look like UI. Some non-UI stock code
  imports menu settings.

## 3. Classify The Stock Digital Factory Python Dependency

### Why This Is Low-Hanging

The full stock Digital Factory connector is large and cloud-facing. It was
measured in the stock baseline at about 33.5 MB VSZ when running, but the stock
init script does not start it at boot by default; it is started asynchronously
from UI flows. Before porting or replacing it, collect focused lifecycle and
resource evidence.
The immediate de-Pythoning task is classification: determine whether Digital
Factory can stay stopped for local-first Deneb, should be lazy-started only for
explicit cloud actions, or deserves a future native replacement.

### Scope

- [ ] Create a repeatable measurement checklist or helper for Digital Factory
  disabled, idle-not-running, pairing, connected/reconnecting, and disconnect
  states.
- [ ] Record process memory, CPU, fd count, thread count, sockets, log growth,
  and service status for each state.
- [ ] If the measurement helper includes native parsing, summarizing, or audit
  code, run it under Valgrind or sanitizers in host mode.
- [ ] Confirm which Deneb UI and web/API workflows require the stock connector
  versus only the native bridge.
- [ ] Decide whether the next de-Pythoning action is native replacement, lazy
  start/stop control, documentation only, or leaving stock behavior in place.

### Acceptance Criteria

- [ ] Evidence distinguishes "Digital Factory bridge reachable" from "Digital
  Factory cloud connector lifecycle proven."
- [ ] Measurements include at least one run where the connector is not running
  and one supervised run where it is started by the intended flow.
- [ ] Logs and process samples are saved or summarized in the relevant evidence
  doc.
- [ ] Any new native measurement helper has clean memory-tool evidence or a
  documented reason why host memory tooling is not practical.
- [ ] The team has a clear go/no-go recommendation for a future native connector
  port.

### Suggested Validation

- [ ] Collect `/etc/init.d/digitalfactory enabled`, service status, process list,
  and relevant `/var/log/ultimaker/digitalfactory.log*` lines before starting.
- [ ] Use the Deneb Digital Factory screen to request status/pairing.
- [ ] Collect process and log samples during pairing and after disconnect.
- [ ] Confirm local-first Deneb workflows remain usable when the connector is
  stopped.

### Risks And Guardrails

- Digital Factory touches cloud connectivity and printer identity. Do not port
  it from stock code or guess at cloud protocol behavior.
- Keep this as measurement first. A native replacement should only start after
  the team understands the needed product behavior and privacy/security model.

## 4. Disable Or Bypass Stock Python Compile Work Under Deneb Installs

### Why This Is Low-Hanging

The stock `rootfs/etc/init.d/compile_all` service runs
`python3 -m compileall /home/cygnus` when `/home/cygnus/__pycache__` is absent.
Deneb already prunes the stock Python touchscreen and is reducing Python service
usage. This should be handled after the UI prune boundary and Digital Factory
lifecycle are understood, because coordinator and Digital Factory still depend
on stock Python modules today.
This does not remove stock Python source from the read-only image. It removes or
gates avoidable Python startup work in Deneb mode once the remaining active
Python dependencies have been reviewed.

### Scope

- [ ] Confirm whether `compile_all` is currently enabled on the target after a
  Deneb install.
- [ ] Decide whether to disable the init service, replace it with a Deneb-aware
  no-op shim, or gate it behind a marker such as a rollback/stock mode flag.
- [ ] Ensure rollback to stock menu/coordinator behavior does not depend on
  freshly generated pycache.
- [ ] Run Valgrind or sanitizer coverage for any changed native installer-audit
  helper, package selftest, or Deneb-gating tool involved in this change.
- [ ] Add installer logging that clearly says whether `compile_all` was left
  enabled, disabled, or bypassed.

### Acceptance Criteria

- [ ] Deneb install leaves `compile_all` disabled or Deneb-gated only after
  native UI smoke, native print-service install checks, and Python dependency
  review pass.
- [ ] A reboot after install does not run broad `/home/cygnus` compileall work.
- [ ] Rollback or stock fallback remains understandable and documented.
- [ ] Boot logs show the chosen Deneb behavior.
- [ ] Memory-tool runs for changed native/test code are clean or have documented
  expected suppressions.

### Suggested Validation

- [ ] Before change: collect `ls -l /etc/rc.d | grep compile_all`,
  `/etc/init.d/compile_all enabled`, and boot log lines mentioning compileall.
- [ ] Install the changed package and reboot.
- [ ] Re-check init enablement, boot logs, process list, and `/home/cygnus`
  pycache churn.
- [ ] Confirm `deneb-ui`, `deneb-api`, `deneb-web`, `deneb-mdns`,
  `deneb-printsvc`, coordinator-backed flows, and Digital Factory measurement
  paths still work.

### Risks And Guardrails

- Do not disable this blindly on a stock-only device. Gate it through Deneb
  install state or explicit package ownership.
- If coordinator or Digital Factory still imports stock Python modules, they
  should keep working from source files without pycache. Verify this on target.
- Disabling the init service can avoid runtime compile work, but it does not
  delete stock Python source files from read-only firmware.

## 5. Audit Non-Python Service Disables Separately From De-Pythoning

### Why This Is Low-Hanging

`onion-helper` is a small non-Python boot service. Earlier audit notes found it
around 1.2 MB RSS but only about 100 KiB anonymous/private memory. That makes it
a possible cleanup experiment, not a de-Pythoning task and not a high-value
native port.

### Scope

- [ ] Reconfirm current `onion-helper` process memory and private memory on the
  target.
- [ ] List ubus objects/methods exposed by the service.
- [ ] Search live workflows and logs for references to `onion-helper`.
- [ ] Run memory tooling for any new native audit or measurement helper created
  for the experiment.
- [ ] Run a supervised stop/disable test only on development hardware.
- [ ] Decide whether to leave enabled, disable only in experimental packages, or
  keep as an explicit non-target.

### Acceptance Criteria

- [ ] The team has fresh before/after service, ubus, process, log, and workflow
  evidence.
- [ ] Deneb UI, web/API, print service, networking, update install, and printer
  identity checks still work during the supervised stop test.
- [ ] Any disable behavior is experimental, reversible, and logged by the
  installer.
- [ ] Any new native helper has clean Valgrind/sanitizer evidence or a documented
  reason why host memory tooling is not practical.

### Suggested Validation

- [ ] Collect `ps`, `/proc/<pid>/status`, `/proc/<pid>/smaps` summary if
  available, `ubus list`, and `logread` before stopping the service.
- [ ] Stop the service without disabling it permanently, then run Deneb smoke
  checks.
- [ ] Reboot once with the service enabled again to confirm normal recovery.
- [ ] Only after a clean supervised test, consider an experimental disable gate.

### Risks And Guardrails

- The likely private-memory win is small. Do not spend much time here unless
  measurement shows a real benefit.
- Avoid disabling it by default until the broader Onion/OpenWrt service
  dependency picture is understood.
- Treat this as a service enablement decision, not a deletion task; the stock
  binary and init script may live in the read-only base image.

## Suggested Work Order

1. Remove the Deneb-owned Python Digital Factory bridge from all runtime and
   package paths, then add package/audit protection.
2. Make stock Python UI pruning explicit and auditable, including a retained
   dependency list.
3. Classify the stock Digital Factory Python dependency with lifecycle/resource
   evidence before choosing a porting project.
4. Disable or bypass `compile_all` only after the remaining Python dependency
   picture is clearer.
5. Keep `onion-helper` separate as a non-Python supervised service experiment.

## Handoff Notes

- The first two tasks are cleanup/audit tasks that should be suitable for short
  branches.
- The third task is evidence collection and should feed the next service-port
  decision.
- The fourth task is deliberately later because stock coordinator and Digital
  Factory can still rely on source Python imports.
- The fifth task should not be promoted unless fresh measurements show the small
  memory win is worth the risk.
- None of these tasks should promise read-only firmware space savings. The wins
  are boot behavior, live process count, RAM/CPU/log churn, package cleanliness,
  and reduced active dependency on stock Python paths.
- Because Deneb is resource-sensitive, every task should leave behind both
  memory-tool evidence for changed native/test code where practical and target
  resource evidence for changed runtime behavior.
- Success for this checklist means the team can point at each remaining Python
  path and say whether it is removed from Deneb runtime, quarantined as
  reference/tooling, disabled/avoided in Deneb mode, or still required by an
  unreplaced stock service.
