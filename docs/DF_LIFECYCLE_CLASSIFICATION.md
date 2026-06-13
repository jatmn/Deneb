# Digital Factory Lifecycle Classification Evidence

Task #3: Classify the stock `connector.py` Python Digital Factory cloud
connector and define the path to remove it from active Digital Factory use.

## Status Summary

**Decision: the active-use C connector is implemented and packaged; live cloud
validation is partially proven.**
The Deneb-owned Digital Factory bridge is native, and Deneb now builds
`deneb-dfsvc`, a C service that owns the long-running WebSocket path previously
handled by stock `connector.py`. The package replaces
`/etc/init.d/digitalfactory` with a native init script, installs
`/usr/bin/deneb-dfsvc`, and audits fail if Deneb ships or starts a Python
Digital Factory connector fallback.

This closes the source/package/install side of the Digital Factory C port.
On-target evidence now proves the disabled/unpaired baseline, the live
pairing-PIN flow, and connected steady-state through `deneb-dfsvc` without
stock `connector.py`. The remaining blockers before marking the lifecycle fully
proven are remote print and print-job action validation.

## Workflow Analysis: Native Bridge vs Stock Connector

### Deneb Architecture

| Layer | Component | What it does | Zero-footprint? |
|-------|-----------|--------------|-----------------|
| **Native bridge** | `deneb-api digital-factory` | One-shot CLI; talks ZMQ to the stock Gershwin coordinator IPC to read DF state and send connect/disconnect RPCs. No lingering process. | Yes — runs and exits |
| **Deneb API runtime** | `deneb-api` HTTP service | Owns local UM/Cura compatibility APIs plus the DF command-mode binary entry point. It is not currently a DF cloud endpoint, but it already contains the native DF bridge code and linked coordinator IPC stack. | Yes for bridge commands — one-shot mode exits |
| **Native connector** | `deneb-dfsvc` via `/etc/init.d/digitalfactory` | Long-running C service; maintains the WebSocket to Digital Factory cloud, handles pairing/status, routes cloud prints through Deneb cluster upload/start, routes print-job actions through Deneb native action rules, and handles printer rename. | No — runs continuously |
| **Stock connector baseline** | `connector.py` via stock `/etc/init.d/digitalfactory` | Historical Python reference (~33.5 MB VSZ); maintained the WebSocket to Digital Factory cloud using `stardustWebsocketProtocol`. | No — replaced in the Deneb package |
| **Coordinator** | `coordinator.py` | Stock Gershwin IPC node graph. Multiplexes IPC between the bridge (ZMQ) and the connector (Gershwin internal protocol). | No — runs continuously |

### Which Workflows Need What

| Workflow | Native Bridge | Long-Running Connector | Notes |
|----------|---------------|-----------------|-------|
| DF status display | Required | Not required | Bridge reads coordinator IPC directly; connector.py can be stopped |
| DF connect initiation | Required | **Required** | Bridge sends connect RPC; Deneb package starts `deneb-dfsvc` for active WebSocket pairing |
| DF disconnect | Required | **Required** | Bridge sends disconnect RPC; native connector clears cluster state and publishes disconnected status |
| Cloud pairing (live) | Not required | **Required** | `deneb-dfsvc` runs the WebSocket protocol; bridge is still just a control interface |
| Steady-state cloud connection | Not required | **Required** | `deneb-dfsvc` maintains the live WebSocket to DF cloud |
| Local printing (USB/GCode) | Not required | Not required | No DF dependency at all |
| Local network/Cura discovery | Not required | Not required | Uses mDNS, cluster API over HTTP |
| Language switching | Not required | Not required | UI-only operation |
| Diagnostics export | Not required | Not required | Uses `deneb-api` directly |
| Package updates | Not required | Not required | Uses `sysupgrade` / `opkg` |

### Key Insight

The native bridge is **not a replacement** for the stock connector — it is a
thin IPC control interface. The bridge enables the Deneb UI to:
1. Read DF status from the Gershwin coordinator IPC
2. Send connect/disconnect commands to the coordinator

But the actual cloud connectivity, pairing protocol, and WebSocket management
are handled by the long-running connector service. In the Deneb package that
service is now Deneb-owned native C code (`deneb-dfsvc`) instead of
`connector.py` stock Python.
Removal/disablement of Digital Factory cloud pairing is not a valid completion
path for this task unless a separate product decision explicitly drops the
feature.

## Current Lazy Start/Stop Control

The Deneb installer and DF screen already implement proper lifecycle control:

1. **At install time** — `/etc/init.d/digitalfactory` is disabled when
   `ultimaker.option.cluster_id` is empty (no prior pairing)
2. **DF screen "Connect" button** — Enables + starts `digitalfactory` when the
   user initiates connect/pairing, then initiates bridge `connect` RPC
3. **DF screen "Disconnect" button** — Sends bridge `disconnect` RPC; if
   cluster_id was cleared, also stops + disables `digitalfactory`
4. **Boot** — Service stays disabled unless cluster_id is configured

This means the Digital Factory service runs **only when** the user has
explicitly paired the printer with Digital Factory cloud. In current Deneb
packages that active service is the native connector, not stock Python.

## Measurement Helper

`tools/deneb-df-measure.sh` — repeatable on-target measurement script.

```sh
# Print the measurement checklist
./tools/deneb-df-measure.sh --checklist

# Capture a sample for a given state
./tools/deneb-df-measure.sh --state connected --summary /tmp/df-connected.summary
```

The helper captures per sample:
- VSZ, RSS, shared pages, FD count, thread count, CPU jiffies
- PID-owned TCP/UDP socket counts
- Service enabled state, process-backed running state, and raw init-script
  running return
- Digital Factory log file size
- Bridge status output

The `df_running` summary field is intentionally process-backed: it is `1` only
when `deneb-dfsvc` or a stock Python `connector.py` process is present. The raw
`/etc/init.d/digitalfactory running` return is recorded separately as
`df_init_running`, because the stock-style init command can return success even
when no connector process exists.

Designed for all six DF states: disabled, idle-not-running, pairing, connected,
reconnecting, disconnect.

**Memory-tool note**: The helper is pure shell (no native code), so Valgrind or
sanitizer runs are not applicable. If extended with a compiled C helper, the
task checklist requires host-side Valgrind evidence.

## Existing Measurements (from BASELINE_MEASUREMENTS.md)

| Metric | Value | Source |
|--------|-------|--------|
| Stock connector.py VSZ | 33.5 MB (34,328 KB) | Stock idle baseline |
| Bridge `status --timeout 10` peak VSZ | 2,368 KB | One-shot; exits after command |
| Bridge `status --timeout 10` peak RSS | 1,036 KB | One-shot; exits after command |
| Bridge idle footprint | None (exits after command) | No lingering process |
| DF service boot state | Disabled (when unpaired) | `/etc/init.d/digitalfactory enabled` returns rc 1 |

## Current Native Lifecycle Measurements

Captured on hardware on 2026-06-13 after installing
`dist/Deneb_Update_e213599.deneb` and restarting the live touchscreen to
`deneb-ui` version `e213599`.

| State | Evidence | Result |
|-------|----------|--------|
| Disabled/unpaired | `/tmp/df-disabled-e213599-v3.summary` | `df_enabled=0`, process-backed `df_running=0`, bridge `state=disconnected`, `deneb-dfsvc pid=0`, `connector.py pid=0`, DF log bytes `26733`. Raw `df_init_running=1` is retained as an init-script diagnostic, not lifecycle truth. |
| Pairing / enter PIN | User reported touchscreen sequence: "requesting pairing pin", "reconnecting", `pin: 869281`; `/tmp/df-pairing-e213599.summary` | `deneb-api digital-factory status --timeout 20` returned `state=enter_pin pin=869281`. `deneb-dfsvc` was running as PID `13157` with VSZ `3744 KB`, RSS `3032 KB`, 14 FDs, 3 threads, 4 TCP sockets. Stock `connector.py pid=0`. Syslog showed native `deneb-dfsvc` sending `connection_request`, receiving `connection_response`, and logging "Digital Factory pairing PIN received." |
| Connected steady-state | User reported touchscreen changed to connected; `/tmp/df-connected-e213599.summary` and `/tmp/df-connected-e213599-60s.summary` | `deneb-api digital-factory status --timeout 20` returned `state=connected`. `deneb-dfsvc` stayed on PID `13157`; immediate connected sample was VSZ `3744 KB`, RSS `3044 KB`, 14 FDs, 3 threads, 4 TCP sockets, and `connector.py pid=0`. Settled sample about 60 seconds later was VSZ `3744 KB`, RSS `3052 KB`, 14 FDs, 3 threads, 4 TCP sockets, and `connector.py pid=0`. Syslog showed native cloud account confirmation, then repeated status requests with `state=2 pin=0` and native status responses. |
| Disconnect | User reported touchscreen sequence: confirm prompt, "disconnect requested", then "disconnected"; `/tmp/df-disconnect-e213599.summary` | `deneb-api digital-factory status --timeout 20` returned `state=disconnected`. The helper recorded `df_enabled=0`, process-backed `df_running=0`, `deneb-dfsvc pid=0`, and `connector.py pid=0`. The Digital Factory `cluster_id` was absent from UCI after disconnect, and syslog showed `digital_factory action=disconnect result=state=disconnected` followed by `deneb-dfsvc: info: stopped`. Raw `df_init_running=1` remains only the known init-script diagnostic. |
| Reconnect after cloud interruption | `/tmp/df-reconnect-baseline-e213599.summary` and `/tmp/df-reconnect-recovery-e213599.summary` | Started from `state=connected` with native `deneb-dfsvc` PID `17025`, VSZ `3720 KB`, RSS `3024 KB`, 14 FDs, 3 threads, 4 TCP sockets, and `connector.py pid=0`. A temporary iptables block of the active Digital Factory cloud peer `34.49.252.186` changed bridge status to `state=reconnecting`; `deneb-dfsvc` remained running and stock Python stayed absent. After removing the temporary rules, bridge status returned to `state=connected` with the same PID `17025`, VSZ `3728 KB`, RSS `3036 KB`, 14 FDs, 3 threads, 4 TCP sockets, and `connector.py pid=0`. Follow-up verification showed no cloud-block rules left behind. |
| Printer rename | User reported a rename in Digital Factory; `/tmp/df-rename-e213599.summary` | `deneb-api digital-factory status --timeout 20` stayed `state=connected`. `deneb-dfsvc` stayed on PID `17025` with VSZ `3776 KB`, RSS `3068 KB`, 14 FDs, 3 threads, 4 TCP sockets, and `connector.py pid=0`. UCI showed `ultimaker.option.printer_name='Ultimaker-2C-test'`, and `/cluster-api/v1/printers` reported `friendly_name:"Ultimaker-2C-test"`. Syslog showed native `deneb-dfsvc` receiving `printer_action_request` messages and sending `printer_action_response` messages at the rename time, followed by normal connected status responses. |
| Remote print with material mismatch | User reported Digital Factory showed "In transit..." and accurately requested changing material 1 from Generic Tough PLA to Generic PLA; fixed follow-up captured in `/tmp/df-remote-print-material-mismatch-wait-user-action-e213599.summary`; unsafe Continue run captured before reboot in `/tmp/df-remote-print-material-mismatch-continue-printing-e213599.summary` | **Partially proven, print start and touchscreen pause/resume improved; remote job actions still open.** `deneb-dfsvc` downloaded the signed Digital Factory UFP, extracted `3D/model.gcode`, uploaded the extracted G-code through the local cluster API, and kept stock `connector.py pid=0`. The shared print metadata parser read the Cura header material (`EXTRUDER_TRAIN.0.MATERIAL.GUID`) so the existing Cura-style pending conflict path stopped before printing. `/cluster-api/v1/print_jobs` reported `status:"wait_user_action"`, `started:false`, and `configuration_changes_required` with `type_of_change:"material_change"` from Tough PLA to PLA. `/api/v1/printer/status` stayed `idle`; the touchscreen showed the material-mismatch decision prompt. User selected Cancel, after which `/cluster-api/v1/print_jobs` returned `[]`, the pending metadata file was absent, and printer status remained `idle`. On a follow-up run the user selected Continue Anyway; software status changed to `printing`, but the printer skipped expected stock prepare behavior and moved unsafely, requiring a user reboot. Post-incident stock review showed the coordinator prepare path runs `home_and_center_head.gcode`, `M18 Z`, waits for motion, heats/extracts, then sends `G28 Z` before `JOB`; stock `JOB` startup then handles first-50-line `G280` detection, optional no-`G280` filament-to-tip priming, `G90`, `M82`, `G92 E0`, `G0 F9000`, and `G280` prime expansion. Native `deneb-printsvc` now implements that prepare/startup boundary in host-tested code and no longer rejects `G280` jobs as a temporary guard. A later user-supervised run with package `7be1d77` started safely, but the touchscreen Status screen exposed only Stop and the native Stop path returned idle without the expected stock park/home routine. Package `68af57c` added Status-screen Pause/Resume controls and stock-derived Stop cleanup; target testing proved Pause changed to Resume and Stop behaved as expected, but exposed a cold-resume blocker where Resume moved without reheating after Pause cooled the nozzle. Package `072edbc` reasserts the saved nozzle target before waiting and restoring motion; 2026-06-13 supervised target testing proved Resume preheated the nozzle again, waited for it to reach temperature, returned to position, and continued printing. The same start testing exposed a double-Z-home startup delay: the printer homes XYZ, moves to the prepare position, then runs the stock-derived `G28 Z` again. Package `6cd72899` removes the normal-print `M18 Z` release and second `G28 Z`; supervised target testing confirmed no double Z home and normal startup. Native connector PID `31966` sampled VSZ `3768 KB`, RSS `3072 KB`, 14 FDs, 3 threads, 4 TCP sockets, and `connector.py pid=0` in the prompt sample. |

These samples prove the native service starts from the intended touchscreen
pairing flow, reaches the PIN state, completes cloud account confirmation, and
remains connected without a stock Python connector fallback. They also prove
the touchscreen disconnect flow clears pairing state, disables/stops the native
connector, leaves stock `connector.py` absent, and handles cloud-originated
printer rename. They also prove the native connector receives remote print
requests without falling back to Python. They now prove remote print download,
UFP extraction, local queue registration, material-mismatch user-action gating,
and Cancel cleanup. They prove the later supervised native print start did not
repeat the dangerous skipped-prepare failure, but they do **not** yet prove a
complete Digital Factory remote-action lifecycle. The
Continue/start sample exposed a physical safety blocker in native handling of
Cura/stock prepare/startup semantics; host code now implements the stock-derived
sequence and later target testing started safely. Startup double-Z-home cleanup
is now target-proven on package `6cd72899`: startup no longer double homes Z
and starts as expected. Remote print-job action behavior is still unproven.

## Classification Decision

| Option | Verdict | Rationale |
|--------|---------|-----------|
| Native replacement | **Implemented, target validation partially proven** | Digital Factory cloud pairing, connected steady-state, reconnect after cloud interruption, touchscreen disconnect, printer rename, and remote print material-mismatch user-action gating run through `deneb-dfsvc` without shipping or starting the stock Python connector. Native print-service code now implements the stock-derived prepare/startup/`G280` path, touchscreen pause/resume reheating is target-proven, and package `6cd72899` target testing proved startup no longer double homes Z. Remote print-job actions still need closure. |
| Lazy start/stop | **Implemented** | Installer disables at boot when unpaired; DF screen controls enable/start/stop. The native bridge (`deneb-api digital-factory`) is C code, and active cloud use starts the native service. |
| Documentation only | Insufficient | Documentation records the boundary but does not remove Python from active Digital Factory use. |
| Leave stock behavior | Insufficient | Gating reduces idle/local-first footprint, but a paired/active connector still depends on stock Python. |
| Remove/disable cloud pairing | **Out of scope for de-Python completion** | Disabling the feature avoids Python but does not port the connector. It should not be used to close the Digital Factory de-Python task. |

### Recommendation

Close the implementation/package subtask, pairing-PIN proof, and connected
steady-state proof as **native connector built, packaged, started by the
intended touchscreen flow, and connected to the cloud account**, but do not mark
Digital Factory fully proven until remote cloud action validation is captured.
The current connector is properly lifecycle-managed for the states proven so far:
- Zero footprint when unpaired (service disabled at boot)
- Lazy-started only when user initiates DF pairing from the C-native UI
- Stopped/disabled when user disconnects
- Control path goes through the native C bridge (`deneb-api digital-factory`)
- Pairing-PIN and connected cloud connector paths go through native C service
  (`deneb-dfsvc`)

Track the remaining active-use de-Python work as target/cloud proof, not as a
missing package implementation. The native connector must be validated without
copying vendor Python into Deneb C code.

The remaining on-target measurements (touchscreen Stop stock-derived park/home
behavior under any remaining edge cases, then remote print-job action)
should be
collected via
`tools/deneb-df-measure.sh --state <STATE>` before closing or promoting the
native connector, because those samples define whether the current active
connector behavior and cost are acceptable.

## Native Connector Port Scope

The C port preserves the Digital Factory feature instead of removing the UI or
cloud controls. The implemented package work is:

- Reuse the existing Deneb DF/API surfaces where they fit: `web/src/df_bridge.c`,
  `web/src/main.c` command-mode dispatch, and `/usr/bin/deneb-api
  digital-factory <status|connect|disconnect>` are the current native bridge
  contract for UI-side DF operations.
- Avoid drift between touchscreen, web/API, and service behavior. If native
  connector status or lifecycle state becomes shareable, expose it through the
  existing Deneb API/runtime boundary instead of adding a parallel DF control
  stack.
- Add a Deneb-owned native connector service, separate from the one-shot
  `deneb-api digital-factory` command bridge, to maintain the cloud session.
- Replace `/etc/init.d/digitalfactory` ownership so active pairing/connected
  states launch the native connector, not `/usr/bin/python3 connector.py`.
- Preserve existing touchscreen and web-facing behavior: status, pairing PIN,
  reconnecting, connected, and authenticated disconnect controls.
- Route native status through `/cluster-api/v1/printers` and
  `/cluster-api/v1/print_jobs` when `deneb-api` is available, so Digital
  Factory sees the same printer/job model as Cura/Web.
- Route cloud print requests through native download plus the existing
  `/cluster-api/v1/print_jobs` upload/start path.
- Route cloud print-job actions through the existing cluster action endpoint
  so pause/resume/abort/force share Deneb's native action rules. Keep this open
  until Digital Factory action requests are observed on target, because the
  touchscreen Status-screen Pause/Resume and Stop paths are separate local UI
  proof.
- Preserve the stock-observed Digital Factory UUID grammar in `deneb-dfsvc`.
  The connector-local `df_is_guid()` intentionally accepts lowercase UUID
  hexadecimal only (`0-9`, `a-f`) for cloud-originated fields such as
  `job_instance_uuid` and `action_id`. Do not "fix" this to accept uppercase
  based only on generic UUID permissiveness or the shared local print helpers;
  loosen it only if old firmware source or captured Digital Factory traffic
  proves uppercase is valid for this cloud protocol.
- Keep the current native bridge or fold its coordinator IPC operations into
  the native service only after equivalent behavior is proven.
- Add source/package/install audits that fail when Deneb starts or ships a
  Python Digital Factory connector fallback.
- Validate on target in any remaining touchscreen active-print Stop edge cases
  and remote print-job action states before marking Digital Factory de-Python
  complete. Disabled/unpaired,
  pairing-PIN, connected steady-state, reconnect after cloud interruption,
  touchscreen disconnect, printer rename, and remote-print material-mismatch
  wait-user-action plus Cancel are covered by 2026-06-13 hardware evidence.
  The original Continue/start trial is explicitly rejected as safe proof: it
  moved dangerously before native print-service gained host-tested stock-derived
  prepare/startup/`G280` handling. A later package started safely but exposed a
  separate Stop parity gap: the old touchscreen Status screen had no Pause
  button, and Stop did not run the expected stock park/home cleanup. Package
  `68af57c` fixed that surface enough to prove Pause and Stop, but Resume
  attempted to restore cold after Pause cooldown. Package `072edbc` fixed the
  remaining Resume target preservation path and 2026-06-13 supervised target
  testing proved Resume reheated before restoring motion. The same target run
  exposed a double-Z-home startup delay. Package `6cd72899` removes the
  normal-print Z release/re-home path, and supervised target testing confirmed
  no double Z home and normal startup.
