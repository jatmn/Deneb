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
proven are remote print, print-job action, and printer rename validation.

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

These samples prove the native service starts from the intended touchscreen
pairing flow, reaches the PIN state, completes cloud account confirmation, and
remains connected without a stock Python connector fallback. They also prove
the touchscreen disconnect flow clears pairing state, disables/stops the native
connector, and leaves stock `connector.py` absent. They do **not** prove
remote print, print-job action, or rename behavior.

## Classification Decision

| Option | Verdict | Rationale |
|--------|---------|-----------|
| Native replacement | **Implemented, target validation partially proven** | Digital Factory cloud pairing, connected steady-state, reconnect after cloud interruption, and touchscreen disconnect run through `deneb-dfsvc` without shipping or starting the stock Python connector. Remote workflows still need target proof. |
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

The remaining on-target measurements (cloud print, print-job action, printer
rename) should be collected via
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
  so pause/resume/abort/force share Deneb's native action rules.
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
- Validate on target in cloud print, print-job action, and printer rename states
  before marking Digital Factory de-Python complete. Disabled/unpaired,
  pairing-PIN, connected steady-state, reconnect after cloud interruption, and
  touchscreen disconnect are now covered by 2026-06-13 hardware evidence.
