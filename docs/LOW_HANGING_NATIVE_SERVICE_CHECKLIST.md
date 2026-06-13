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

Deneb had two implementations of the UI-side Digital Factory bridge: an
embedded native path in `deneb-ui` and an older Deneb-owned Python helper at
`ui/scripts/deneb-df-bridge.py`. Both covered the same local Gershwin IPC role:
request status, pairing, and disconnect actions from the stock coordinator.

The completed path removes the Python helper and keeps the bridge out of the UI
binary by reusing the existing C `deneb-api` executable as a local command:
`deneb-api digital-factory <status|connect|disconnect>`. This does not replace
the stock cloud connector and does not add a new web/cloud endpoint.

### Scope

- [x] Remove `ui/scripts/deneb-df-bridge.py` from Deneb source/runtime.
- [x] Remove the bridge entry point from `deneb-ui`.
- [x] Provide the bridge as `deneb-api digital-factory` using the existing C
  API binary and stock coordinator IPC.
- [x] Keep release packaging free of Python bridge helpers and standalone
  `deneb-df-bridge` artifacts.
- [x] Disable stock `digitalfactory` at boot when no
  `ultimaker.option.cluster_id` is configured.
- [x] Start/enable stock `digitalfactory` only for explicit user pairing and
  stop/disable it after disconnect.

### Acceptance Criteria

- [x] A built `.deneb` package contains no Deneb-owned `deneb-df-bridge.py`
  artifact.
- [x] `deneb-ui` can still run `status`, `connect`, and `disconnect` Digital
  Factory bridge actions through `deneb-api digital-factory`.
- [x] Package/audit selftests fail on a fixture that includes the Python bridge.
- [x] Package/audit checks fail if a standalone `deneb-df-bridge` binary ships.
- [x] Documentation names `deneb-api digital-factory` as the runtime path.

### Completion Notes

Completed locally on 2026-06-11. The release package contains no Deneb-owned
Python Digital Factory bridge and no standalone `deneb-df-bridge` binary.
`deneb-api digital-factory` is the only Deneb bridge command path. Resource
measurements are recorded in [BASELINE_MEASUREMENTS.md](BASELINE_MEASUREMENTS.md).

### Suggested Validation

- [x] Build a release package.
- [x] Inspect the package file list for `deneb-api`, `deneb-ui`, and any
  unexpected Python or standalone bridge artifacts.
- [x] On hardware, run the Digital Factory command path and confirm it
  returns a compact status line instead of a bridge launch error.
- [x] Capture process list evidence showing no one-shot Python or standalone
  bridge process is left running after the action.
- [x] Record transient RSS/VSZ for `deneb-api digital-factory` in
  [BASELINE_MEASUREMENTS.md](BASELINE_MEASUREMENTS.md).

### Risks And Guardrails

- The bridge intentionally talks to stock coordinator/Digital Factory Gershwin
  IPC. Do not treat this task as replacing the cloud connector.
- This task can remove Deneb-owned Python helper artifacts from Deneb packages,
  but it does not remove stock Python from the vendor read-only rootfs.
- Keep a rollback path: if bridge behavior regresses, the installer should
  still be able to roll back to a known UI/API package.

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

- [X] Enumerate the exact stock menu files Deneb must retain.
- [X] Enumerate the stock menu files that should be hidden or overlay-pruned
  from the live filesystem view after Deneb UI smoke passes.
- [X] Label retained files as "remaining stock Python dependency" and attach the
  stock service or workflow that still needs each one.
- [X] Add an installer selftest or host fixture that proves the retained files
  are present and the pruned directories are absent.
- [N/A] Run Valgrind or sanitizer coverage for any changed native installer-audit
  helper, host fixture, or package selftest that is executable on the host.
  — All changes in this task are shell scripts; no new native code to instrument.
- [X] Add a simple import-dependency check or documented manual check for the
  retained stock Python constants.
- [X] Update docs to describe the retained shared constants versus removed UI
  implementation.

### Acceptance Criteria

- [X] The prune list is explicit and documented.
- [X] The installer keeps only required shared menu configuration files.
- [X] A Deneb package install does not leave dormant stock screen/navigator/UI
  directories visible in the live overlay filesystem view.
- [X] Coordinator/update/material-related stock imports still work where Deneb
  has not replaced them.
- [N/A] Memory-tool runs for changed native/test code are clean or have documented
  expected suppressions. — Only shell scripts changed.

### Suggested Validation

- [X] Run the installer selftest or host fixture (`deneb-stock-menu-prune-selftest`).
- [X] Run the source import-dependency check (`deneb-stock-menu-import-check`).
- [X] On hardware after install, list `/home/cygnus/menu` and verify retained
  files and removed UI directories.
- [X] Run the native UI smoke path.
- [X] Exercise update entry, material import/set material, diagnostics export,
  and any remaining coordinator-backed path that might import stock menu
  settings.

### Completion Evidence

Closed on hardware on 2026-06-11 with `dist/Deneb_Update_7bf0a2d.deneb`.
The package build ran `deneb-stock-menu-prune-selftest` and
`deneb-stock-menu-import-check`. The SSH install completed with
`deneb-ui: installation complete`, `deneb-ui smoke test passed`, and
`pruned stock Python touchscreen UI; retained shared menu_settings`.

After reboot, `/home/cygnus/menu` contained only `menu_settings.py` and
`machine_config.json`; `executor.py`, `controldialog.py`, `machine.py`,
`pylvgl.py`, `screen.py`, `style.py`, `gui_companion/`, `helpers/`, `img/`,
`navigator/`, `screens/`, `templates/`, and `ui_elements/` were absent from
the live overlay view. `deneb-ui`, `deneb-api`, and `deneb-printsvc` were
running from version `7bf0a2d-dirty`.

Remaining stock import validation used the firmware Python paths
`PYTHONPATH=/home:/home/lib` and successfully imported `menu_settings`,
`cygnus.util.host_id`, `cygnus.util.network`, `cygnus.util.ufp_format`,
`coordinator.coordinator`, and the file/firmware/print handling modules that
still consume `GCODE_DIR`, `GCODE_FILE`, `FW_IMG_COPY_TARGET`,
`FW_IMG_EXTRACT_DIR`, `LAN_INTERFACE`, and `WLAN_INTERFACE`.

Workflow checks over SSH exercised the installed update package path,
`/usr/bin/deneb-ui --smoke-test`, material endpoints, set-material UCI
persistence with restore, `api/v1/system/log`, native backend status, and a
diagnostics export using a temporary writable `/mnt/usb` bind mount.

### Risks And Guardrails

- This is cleanup, not a storage-reclaim project. Overlayfs whiteouts can hide
  stock files from the live filesystem view, but they do not shrink or erase the
  vendor read-only squashfs.
- Keep the prune step after native UI smoke so a broken package does not remove
  the stock fallback before Deneb proves it can start.
- Avoid deleting files solely because they look like UI. Some non-UI stock code
  imports menu settings.

## 3. Port The Stock Digital Factory Connector To C

### Why This Belongs In The De-Python Track

The full stock Digital Factory connector is large and cloud-facing. It was
measured in the stock baseline at about 33.5 MB VSZ when running, but the stock
init script does not start it at boot by default; it is started asynchronously
from UI flows. The earlier containment work kept the stock connector stopped for
local-first Deneb, but containment was not completion: active Digital Factory
pairing and connected states still needed a Deneb-owned connector. The current
tree now includes `deneb-dfsvc`, a native C connector service, and the package
path installs a native `/etc/init.d/digitalfactory`. That closes the
source/package/install side of this subtask, but not the live cloud lifecycle
proof.

### Scope

- [x] Create a repeatable measurement checklist or helper for Digital Factory
  disabled, idle-not-running, pairing, connected/reconnecting, and disconnect
  states.
  → `tools/deneb-df-measure.sh --checklist` (see `docs/DF_LIFECYCLE_CLASSIFICATION.md`)
- [ ] Record process memory, CPU, fd count, thread count, sockets, log bytes,
  and service status for each state.
  → Helper supports all state labels and now samples `deneb-dfsvc` as the
    expected native connector process while separately recording any
    `connector.py` fallback as a regression. CONNECTED stock baseline:
    connector.py ~33.5 MB VSZ (BASELINE_MEASUREMENTS.md). Native DISABLED,
    PAIRING-PIN, CONNECTED, DISCONNECT, RECONNECTING, and RENAME captures were
    recorded on hardware on 2026-06-13 with `dist/Deneb_Update_e213599.deneb`.
    Native cloud-print and print-job-action captures remain outstanding.
- [x] If the measurement helper includes native parsing, summarizing, or audit
  code, run it under Valgrind or sanitizers in host mode.
  → Helper is pure shell script (no native code). Memory-tooling not applicable.
- [x] Confirm which Deneb UI and web/API workflows require the stock connector
  versus only the native bridge.
  → Full workflow table in `docs/DF_LIFECYCLE_CLASSIFICATION.md`.
    Key finding: status display needs bridge only; connect/disconnect and all
    cloud connectivity require a long-running connector service. In current
    packages that service is `deneb-dfsvc`, not stock `connector.py`.
- [x] Decide whether the next containment action is native replacement, lazy
  start/stop control, documentation only, or leaving stock behavior in place.
  → **Native replacement plus lazy start/stop is implemented at package level.**
    Installer replaces the Digital Factory init path with `deneb-dfsvc`; the DF
    screen starts it for explicit setup and stops/disables it after disconnect
    when unpaired.
- [x] Define the full active-use de-Python direction for Digital Factory cloud
  pairing.
  → Required direction: native C connector replacement. Removal/disablement of
    Digital Factory cloud pairing is not completion for this task, and accepting
    stock Python would leave the de-Python work open.
- [x] Inventory the stock connector's external contract without copying vendor
  Python into Deneb C code:
  pairing request/response behavior, credentials/config files, cloud endpoints,
  TLS behavior, reconnect/backoff, coordinator IPC, logs, and failure states.
  → Implemented in the `deneb-dfsvc` C port and documented in
    `docs/DF_LIFECYCLE_CLASSIFICATION.md`. Live behavior still needs capture
    before claiming cloud lifecycle parity.
- [x] Inventory the existing Deneb Digital Factory C/API surface and reuse it
  where relevant:
  `web/src/df_bridge.c`, `web/src/main.c` command-mode dispatch,
  `/usr/bin/deneb-api digital-factory <status|connect|disconnect>`, and the
  touchscreen `screen_digital_factory.c` call sites.
- [x] Add a Deneb-owned native connector service/binary that can maintain the
  active Digital Factory cloud session.
- [x] Replace the active `/etc/init.d/digitalfactory` Python launch path with
  the native connector while preserving rollback evidence.
- [x] Update the touchscreen Digital Factory screen to activate the native
  connector service for pairing and connected states without exposing a manual
  restart control.
- [x] Add source/package/install/runtime audits that fail if Deneb launches or
  packages a Python Digital Factory connector fallback.
- [x] Add drift checks so touchscreen DF controls, the `deneb-api
  digital-factory` command bridge, and the native connector service report
  compatible status/lifecycle states instead of growing separate truth sources.
  → Evidence: commit `1415245` adds `dfsvc/src/main.c`,
    `dfsvc/init/digitalfactory.init`, package staging in `ui/build-package.sh`,
    release wrapper/audit checks, native audit selftests, and DF screen lifecycle
    changes. Host test logs from `printsvc/build-host/Testing/Temporary/LastTest.log`
    include passes for native connector source/init presence, installer native
    init replacement, package rejection of Python DF bridge artifacts, and the
    DF screen calling the native bridge connect/disconnect commands.

### Acceptance Criteria

- [x] Evidence distinguishes "Digital Factory bridge reachable" from "Digital
  Factory cloud connector lifecycle proven."
  → Bridge is one-shot ZMQ IPC client (zero idle footprint). Connector.py is
    the long-running cloud WebSocket manager. Distinguished in workflow table.
- [x] Measurements include at least one run where the connector is not running
  and one supervised run where it is started by the intended flow.
  → 2026-06-13 hardware evidence: `/tmp/df-disabled-e213599-v3.summary`
    recorded disabled/unpaired with `deneb-dfsvc pid=0`, `connector.py pid=0`,
    and bridge `state=disconnected`. `/tmp/df-pairing-e213599.summary`
    recorded touchscreen-initiated pairing with bridge
    `state=enter_pin pin=869281`, `deneb-dfsvc` PID `13157`, VSZ `3744 KB`,
    RSS `3032 KB`, 14 FDs, 3 threads, 4 TCP sockets, and `connector.py pid=0`.
- [x] Logs and process samples are saved or summarized in the relevant evidence
  doc.
  → `docs/DF_LIFECYCLE_CLASSIFICATION.md` summarizes the disabled and pairing
    samples, including the native syslog evidence for connection request,
    connection response, pairing PIN receipt, cloud account confirmation, and
    connected status responses, controlled cloud interruption, reconnect
    recovery, disconnect request handling, service stop, and cleared pairing
    state, and printer rename request handling. Remote print with an accurate
    Digital Factory material mismatch warning now downloads through
    `deneb-dfsvc`, extracts the UFP to G-code, enters the Cura-style
    `wait_user_action` material-mismatch prompt, and Cancel clears the pending
    job while the printer remains idle. Continue Anyway is **not** accepted as
    successful proof: the attempted run entered software `printing` state but
    skipped expected stock prepare behavior and moved dangerously. Stock review
    found the missing boundary in `printhandling.py` plus `marlin_executor.py`:
    home/center, Z release, heat/extract, Z re-home, then `JOB` startup and
    `G280`/prime handling. Native `deneb-printsvc` now implements that
    prepare/startup path with host coverage. Target physical validation and
    print-job action samples still need capture.
- [x] Any new native measurement helper has clean memory-tool evidence or a
  documented reason why host memory tooling is not practical.
  → Helper is pure shell. Documented in evidence doc.
- [x] The team has a clear go/no-go recommendation for the connector path.
  → Go path implemented: port the active Digital Factory connector to native C.
    Remaining work is live cloud proof, not deciding whether to port.
- [x] Active pairing and connected states run without `connector.py`,
  `/usr/bin/python3 connector.py`, or `stardustWebsocketProtocol` imports.
  → 2026-06-13 hardware evidence: pairing-PIN and connected steady-state both
    ran through native `deneb-dfsvc` PID `13157`, and helper summaries recorded
    `connector.py pid=0`.
- [x] Digital Factory pairing, status, reconnect, and authenticated disconnect
  behavior are validated on target against the native connector.
  → Touchscreen status/connect reached `state=enter_pin pin=869281`, then
    `state=connected`, through native `deneb-dfsvc`. Touchscreen disconnect
    requested and reached `state=disconnected`, cleared `cluster_id`, disabled
    the service, stopped `deneb-dfsvc`, and left `connector.py pid=0`.
    Controlled cloud interruption changed status to `state=reconnecting`;
    removing the temporary block returned status to `state=connected` with the
    same native `deneb-dfsvc` PID and `connector.py pid=0`.
- [ ] The existing native bridge/API command path remains the shared control
  boundary for UI-side status/connect/disconnect unless a tested replacement
  deliberately consolidates it with the native connector.

### Suggested Validation

- [x] Collect `/etc/init.d/digitalfactory enabled`, service status, process list,
  and relevant `/var/log/ultimaker/digitalfactory.log*` lines before starting.
  → Use `tools/deneb-df-measure.sh --state disabled` to capture baseline.
- [x] Use the Deneb Digital Factory screen to request status/pairing.
  → Use `tools/deneb-df-measure.sh --state pairing` or `--state connected`.
- [x] Collect process and log samples during pairing and after disconnect.
  → Pairing-PIN, connected, and disconnect samples captured on 2026-06-13.
- [ ] Confirm local-first Deneb workflows remain usable when the connector is
  stopped.
  → Verified by analysis: all local-first workflows (printing, USB, language,
    diagnostics, updates) have zero DF dependency.

### Risks And Guardrails

- Digital Factory touches cloud connectivity and printer identity. Keep the C
  connector grounded in observed contracts and measurements; do not copy stock
  Python implementation into Deneb native code or guess at cloud protocol
  behavior.
- Do not mark Digital Factory fully de-Pythoned until on-target remote print and
  print-job action states are validated with `deneb-dfsvc` and no stock
  `connector.py` fallback.
  Disabled/unpaired, pairing-PIN, and connected steady-state are now covered by
  2026-06-13 hardware evidence. Reconnect after controlled cloud interruption
  and touchscreen disconnect are covered by the same run. Printer rename is
  also covered by that run. Remote print download/UFP extraction, material
  mismatch wait-user-action gating, and Cancel cleanup are covered by the
  2026-06-13 hardware run. Continue/start after the prompt is reopened as a
  target-validation blocker: native `deneb-printsvc` now has host-tested
  stock-derived prepare/startup/`G280` handling, but it still needs supervised
  deployment proof before this gate closes. A later package started safely, but
  exposed a separate active-print UI/Stop parity blocker: the Status screen had
  no Pause button, and Stop returned idle without the expected stock park/home
  routine. Native source now adds Status-screen Pause/Resume controls and a
  stock-derived abort cleanup policy, but deployment plus supervised target
  proof is still required. Remote print-job actions also remain open closure
  gates.

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

- [X] Confirm whether `compile_all` is currently enabled on the target after a
  Deneb install — confirmed: stock compile_all init script exists at
  `rootfs/etc/init.d/compile_all` with `START=90`, and the stock image ships
  an enabled `/etc/rc.d/S90compile_all` entry (`rootfs.manifest`). The previous
  installer did not handle it, so it remained enabled after Deneb install.
- [X] Approach chosen: disable the init service (standard `rc.d` disable), back up
  the original init script, and clear `/home/cygnus/__pycache__` so that a
  re-enabled compile_all on stock rollback re-runs on next boot. No no-op shim
  or gating marker needed — `rc.d` disable is clean and reversible.
- [X] Rollback safety confirmed: stock compile_all only runs when `__pycache__` is
  absent. Deneb clears pycache on disable, so restored stock init will re-run
  compile_all on next boot after rollback once the rc.d link is re-enabled. If
  pycache already exists (e.g. from initial stock boot), compile_all is a no-op
  anyway.
- [X] No native code changed — this is purely shell-script changes to the
  installer (`ui/installer/update.sh`) and a host-fixture selftest
  (`tools/deneb-compile-all-selftest.sh`). Valgrind/sanitizer not applicable.
- [X] Installer logging added: logs enabled/disabled/absent state, backup action,
  and pycache-clearing action.

### Acceptance Criteria

- [X] Deneb install leaves `compile_all` disabled — `prune_stock_compile_all()`
  stops, disables, and backs up the stock init script; removes pycache.
- [X] A reboot after install does not run broad `/home/cygnus` compileall work —
  disabled init means the service never fires, confirmed by
  `/etc/init.d/compile_all enabled` returning `1` after reboot.
- [X] Rollback or stock fallback remains understandable and documented — backup at
  `DENEB_BACKUP_DIR/compile_all.init.orig`; pycache cleared so re-enable
  triggers fresh compile on next boot.
- [X] Boot logs show the chosen Deneb behavior — installer logs emitted via
  `log()` while running `prune_stock_compile_all`.
- [N/A] Memory-tool runs for changed native/test code are clean or have documented
  expected suppressions. — No native code changed. Host fixture selftest and
  release package build passed.

### Suggested Validation

- [X] Before change: collect `ls -l /etc/rc.d | grep compile_all`,
  `/etc/init.d/compile_all enabled`, and boot log lines mentioning compileall.
- [X] Install the changed package and reboot.
- [X] Re-check init enablement, boot logs, process list, and `/home/cygnus`
  pycache churn.
- [X] Confirm `deneb-ui`, `deneb-api`, `deneb-web`, `deneb-mdns`,
  `deneb-printsvc`, API status, and cluster print-job listing still work.
- [ ] Exercise deeper coordinator-backed flows and Digital Factory measurement
  paths after this change if those paths become part of the release gate.

### Completion Evidence

Closed on hardware on 2026-06-11 with `dist/Deneb_Update_93bcac1.deneb`.
The package build completed the native print-service build, release package
audits, `deneb-stock-menu-prune-selftest`, `deneb-stock-menu-import-check`, and
release package assembly. The new host fixture
`tools/deneb-compile-all-selftest.sh` passed separately before the SSH install.

The first SSH install run was cut off by a local 120 second timeout while still
running packaged selftests. Rerunning the same package with a longer timeout
completed successfully with `deneb-ui: installation complete`, including
`stock compile_all is enabled — disabling and backing up`,
`compile_all disabled: stock /home/cygnus compileall will not run at boot`, and
`cleared /home/cygnus/__pycache__ so re-enabled compile_all re-runs on next stock boot`.

After reboot, `deneb-printsvc`, `deneb-ui`, and `deneb-api` were running from
version `93bcac1-dirty`; `/api/v1/printer/status` returned `"idle"`, and
`/cluster-api/v1/print_jobs` returned `[]`. `deneb-api`, `deneb-web`,
`deneb-printsvc`, and `deneb-ui` were enabled (`rc=0`). `compile_all` was
disabled (`/etc/init.d/compile_all enabled` returned `1`), no
`/etc/rc.d/S90compile_all` or `K90compile_all` link existed, the stock init was
backed up at `/home/deneb/backups/deneb-ui/compile_all.init.orig`, and
`/home/cygnus/__pycache__` was absent.

### Risks And Guardrails

- Do not disable this blindly on a stock-only device. Gate it through Deneb
  install state or explicit package ownership.
- If coordinator or Digital Factory still imports stock Python modules, they
  should keep working from source files without pycache. Re-verify those
  specific import paths on target when changing their service ownership.
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
3. Validate the native Digital Factory connector on target, using
   lifecycle/resource evidence to prove `deneb-dfsvc` preserves behavior and
   that the Python connector no longer launches during active cloud use.
4. Disable or bypass `compile_all` only after the remaining Python dependency
   picture is clearer.
5. Keep `onion-helper` separate as a non-Python supervised service experiment.

## Handoff Notes

- The first two tasks are cleanup/audit tasks that should be suitable for short
  branches.
- The third task's source/package/install work is done; its remaining work is
  live cloud evidence collection and parity validation.
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
