# Digital Factory Lifecycle Classification Evidence

Task #3: Classify the stock `connector.py` Python Digital Factory cloud
connector and define the path to remove it from active Digital Factory use.

## Status Summary

**Decision: containment is implemented; the required active-use fix is a native
C Digital Factory connector.**
The Deneb-owned Digital Factory bridge is native, and the stock `connector.py`
is gated so it is disabled at boot when unpaired, lazy-started from the DF
screen on connect, and stopped on disconnect. This removes Python from
local-first and idle/unpaired Digital Factory paths, but **Digital Factory still
uses stock Python whenever the cloud connector is active**. The remaining
de-Pythoning task is to port the Digital Factory cloud connector and related
active-use runtime paths to C while preserving Digital Factory pairing,
authentication, reconnect, disconnect, and steady-state cloud behavior.

## Workflow Analysis: Native Bridge vs Stock Connector

### Deneb Architecture

| Layer | Component | What it does | Zero-footprint? |
|-------|-----------|--------------|-----------------|
| **Native bridge** | `deneb-api digital-factory` | One-shot CLI; talks ZMQ to the stock Gershwin coordinator IPC to read DF state and send connect/disconnect RPCs. No lingering process. | Yes — runs and exits |
| **Deneb API runtime** | `deneb-api` HTTP service | Owns local UM/Cura compatibility APIs plus the DF command-mode binary entry point. It is not currently a DF cloud endpoint, but it already contains the native DF bridge code and linked coordinator IPC stack. | Yes for bridge commands — one-shot mode exits |
| **Stock connector** | `connector.py` via `/etc/init.d/digitalfactory` | Long-running Python process (~33.5 MB VSZ); maintains the WebSocket to Digital Factory cloud, handles pairing, authentication, reconnection. Uses the proprietary `stardustWebsocketProtocol` (56 files). | No — runs continuously |
| **Coordinator** | `coordinator.py` | Stock Gershwin IPC node graph. Multiplexes IPC between the bridge (ZMQ) and the connector (Gershwin internal protocol). | No — runs continuously |

### Which Workflows Need What

| Workflow | Native Bridge | Stock Connector | Notes |
|----------|---------------|-----------------|-------|
| DF status display | Required | Not required | Bridge reads coordinator IPC directly; connector.py can be stopped |
| DF connect initiation | Required | **Required** | Bridge sends connect RPC → coordinator → connector.py must be running to execute the WebSocket pairing |
| DF disconnect | Required | **Required** | Same path: bridge RPC → connector.py handles cloud-side teardown |
| Cloud pairing (live) | Not required | **Required** | connector.py runs the stardust WebSocket protocol; bridge is just a control interface |
| Steady-state cloud connection | Not required | **Required** | connector.py maintains the live WebSocket to DF cloud |
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
are handled entirely by `connector.py` (stock Python). A full active-use
de-Python path must replace that connector with Deneb-owned native C code.
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

This means connector.py runs **only when** the user has explicitly paired the
printer with Digital Factory cloud. It is containment, not a native replacement.

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
- Service enabled/running state
- Digital Factory log file size
- Bridge status output

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

## Classification Decision

| Option | Verdict | Rationale |
|--------|---------|-----------|
| Native replacement | **Required for full active-use de-Python** | Digital Factory cloud pairing must remain available without Python. High-risk because the current behavior lives in stock cloud/protocol Python and must be replaced by a native C implementation. |
| Lazy start/stop | **Containment done** | Installer disables at boot when unpaired; DF screen controls enable/start/stop. The native bridge (`deneb-api digital-factory`) is already C code, but active cloud use still starts Python. |
| Documentation only | Insufficient | Documentation records the boundary but does not remove Python from active Digital Factory use. |
| Leave stock behavior | Insufficient | Gating reduces idle/local-first footprint, but a paired/active connector still depends on stock Python. |
| Remove/disable cloud pairing | **Out of scope for de-Python completion** | Disabling the feature avoids Python but does not port the connector. It should not be used to close the Digital Factory de-Python task. |

### Recommendation

Close the containment subtask as **lazy start/stop control implemented**, but do
not close Digital Factory as fully de-Pythoned. The current connector is
properly lifecycle-managed:
- Zero footprint when unpaired (service disabled at boot)
- Lazy-started only when user initiates DF pairing from the C-native UI
- Stopped/disabled when user disconnects
- Control path goes through the native C bridge (`deneb-api digital-factory`)

Track the active-use de-Python task as a native C connector port. That port must
own the active Digital Factory connection path currently implemented by
`connector.py` and the `stardustWebsocketProtocol` Python package, without
copying vendor Python into Deneb C code.

The remaining on-target measurements (pairing, connected steady-state,
reconnecting, disconnect) should be collected via
`tools/deneb-df-measure.sh --state <STATE>` before scoping the replacement,
because those samples define the current active connector behavior and cost.

## Native Connector Port Scope

The C port should preserve the Digital Factory feature instead of removing the
UI or cloud controls. The expected implementation work is:

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
  reconnecting, connected, disconnect, and restart controls.
- Keep the current native bridge or fold its coordinator IPC operations into
  the native service only after equivalent behavior is proven.
- Add source/package/install audits that fail when Deneb starts or ships a
  Python Digital Factory connector fallback.
- Validate on target in disabled, pairing, connected, reconnecting, and
  disconnect states before marking Digital Factory de-Python complete.
