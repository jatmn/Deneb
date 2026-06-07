# Resource Reduction Plan

Deneb assumes the stock firmware is already too constrained by RAM, CPU, boot time, and UI latency. Matching stock behavior is not the final goal; measurable reduction is.

## First Measurements After SSH Bootstrap

- Process list and resident memory.
- Idle CPU and wakeups.
- CPU while printing.
- Free RAM and memory pressure while idle, printing, uploading, updating, and exporting logs.
- Boot-to-ready timing.
- Service startup order.
- Open sockets and listeners.
- Flash and local storage usage.
- Log volume and rotation behavior.

## Release Gates

- New long-running services need measured RAM and CPU budgets.
- Hot paths should prefer event-driven behavior over polling.
- Large files, logs, thumbnails, and uploads should be streamed or parsed in bounded memory.
- Stable releases should block resource regressions unless explicitly scoped as temporary transition builds.

## Current Guardrails

- Stock menu baseline: 33.7 MB VSZ / about 21 MB RSS.
- Current Deneb UI idle snapshot: 2.7-2.8 MB VSZ / about 1.5-2 MB RSS for `deneb-ui --lang en`.
- Current settled idle system sample: about 90% idle after Deneb replaces the stock menu.
- Current `.deneb` package size: about 1.8 MiB after release stripping, with LVGL, static ZMQ, generated i18n fonts, and the embedded C Digital Factory bridge.
- Current update releases also bundle the lightweight web runtime (`lighttpd`,
  `deneb-api`, static web assets, and `deneb-mdns`) plus Cura local-network
  compatibility code. Resource validation for web polling, cluster API polling,
  upload/start, and print actions remains required before Stable release gates
  should rely on the current budgets.
- Deneb installer prunes the dormant stock Python touchscreen UI after a successful native UI smoke test, saving about 884 KiB on disk while preserving `cygnus.menu.menu_settings` for backend imports.
- Deneb installer disables the stock WiFi AP/captive-portal path and hides the
  obsolete `wificonnect`, `nodogsplash` htdocs, and `cygnus-web-assets` trees
  from the live filesystem. These files live in the read-only squashfs base, so
  `.deneb` cleanup removes runtime visibility/reachability but does not reclaim
  base-image flash space.
- Deneb networking is client-only. The installer disables AP-side DHCP/DNS and
  IPv6 router-advertisement services (`dnsmasq` and `odhcpd`) while preserving
  client networking through `netifd` and `udhcpc`, and removes the stale
  `dhcp.wlan` AP DHCP scope. The `dnsmasq` and `odhcpd` binaries remain in the
  read-only base image; the RAM and attack-surface win comes from stopping and
  disabling them.
- Current remaining idle RAM pressure is dominated by stock Python backend
  services: `coordinator.py` at about 22.8 MB RSS and `print_service.py` at
  about 15.1 MB RSS on the latest sample. `print_service.py` is the Marlin
  serial bridge and motion-controller command/status service, so it is a
  clean-room rewrite candidate rather than something to disable.

## Next Measurement Targets

- Test whether the persistent `logread -f -F /var/log/ultimaker/system.log`
  mirror is still needed when diagnostics can capture `logread` output on
  demand; the latest sample shows about 0.8 MB RSS.
- Dedicated milestone: de-python `marlindriver` by building a native
  `deneb-printsvc` replacement for `print_service.py`; the working checklist
  is [UM2C_MODDING_CHECKLIST.md Section 8](../UM2C_MODDING_CHECKLIST.md#8-de-python-marlindriver--native-print-service).
  The live investigation shows it owns `/dev/ttyS1`, verifies/programs the
  motion-controller firmware at startup, handles job/macro/G-code control,
  publishes live status on ZMQ, and implements Marlin flow control,
  CRC/framing, resend handling, and pause/resume state. It is a meaningful RAM
  target, but high risk.
- Keep the first native print-service stage compatible with the stock
  print-service contract: status PUB on `127.0.0.1:5555`, command REP on
  `127.0.0.1:5556`, topic `10001`, and raw `COMMAND<json>` framing. This lets
  coordinator, LCD UI, web UI, Cura LAN flow, and Digital Factory assumptions
  remain stable while the Marlin-facing implementation changes underneath.
- The milestone must also unwind Deneb's current patchwork around the Python
  driver. LCD `backend_comm`, web `backend_zmq`, `api_print_job`,
  `api_cluster`, `api_printer`, direct macro calls, raw G-code calls, and any
  remaining status classifiers all need an ownership decision: keep as clients,
  move into `deneb-printsvc`, or replace with a shared Deneb print-control API.
  Conflict/preheat action bridges and pending-job metadata have started moving
  into shared native helpers instead of embedded Python/Gershwin launchers.
- Do not preserve awkward compatibility layers just because they match the
  current Python driver's shape. Any shim kept for migration needs explicit
  removal criteria and tests proving the final Deneb-owned contract is cleaner
  for web UI, touchscreen UI, API, and Cura LAN flows.
- Deduplicate print-control behavior while the native service is introduced.
  Status classification, print/pending job metadata, command formatting, macro
  lookup, safe motion policy, heat-state decisions, pause/resume/abort
  semantics, and error mapping should each have one owner. Shared native helpers
  now cover command formatting, pending-job files, print-state rules, shared
  stock macro names, and web/API status labels. Web and touchscreen macro,
  multi-line G-code, and job-start callers now route through native backend
  helper functions instead of each hand-rolling stock command JSON. LCD/API
  job-name display also reads pending-job metadata through the shared helper
  instead of local JSON scans, and the pending display-name fallback has one
  shared owner for name/path-basename/`"none"` handling across LCD backend
  status retention, touchscreen labels, and web/API printer responses. LCD and web/API backends now select native
  `deneb-printsvc` status/command ports directly when `deneb.printsvc.enabled=1`,
  while preserving the stock coordinator route as the default fallback. That
  route decision lives in `common/print/print_backend_route.*` so clients do not
  each duplicate UCI/env parsing or endpoint constants. The same helper now
  formats route diagnostics exposed by web status JSON and LCD/web backend
  accessors, giving lab runs a native way to prove whether a process selected
  stock coordinator or native `deneb-printsvc`. Later slices should
  keep collapsing remaining
  duplicate web/UI/API logic toward those helpers or a single
  `deneb-printsvc` API.
- Keep `deneb-printsvc` source files split by responsibility from the first
  scaffold. Expected modules include service/init, ZMQ IPC, print-control API,
  serial transport, Marlin packet framing, CRC, status parsing, command
  parsing, G-code stream reader, macro registry, job queue, heater waits,
  pause/resume state, abort/finalize motion policy, diagnostics/logging, and
  test fakes. Avoid thousand-line catch-all files.
- Treat abort, pause, resume, and print-finish as intentional Deneb behavior,
  not line-for-line ports. The native replacement must remove unsafe abort
  cleanup behavior, avoid duplicate homing, and report clear cancellation
  status before it can replace stock `printserver` outside experimental builds.
- Native diagnostic logging now writes a low-volume comparison stream under
  `/var/log/ultimaker/deneb-printsvc.log` when the lab-gated service is
  running. Each status line places stock-shaped fields such as `stock.req`,
  `stock.file`, temperatures, position, and fault state beside native phase,
  stop-allowed state, serial ACK/reject/resend counters, queue depth, streamed
  line number, command latency, and planner-starvation counters. This gives
  lab runs a stable artifact for comparing native behavior against captured
  stock `printserver` logs without adding Python.
- The native replacement now has an initial buildable C source tree at
  `printsvc/`, is included in release packages as a lab-gated binary, and has
  host tests for the command/status/packet/flow-control/heater-wait,
  G28/home-distance, nonblocking job streaming, motion-firmware verification,
  abort/finish policy, pause/resume state-machine behavior, shared print-control
  contract, shared command formatting, pending-job metadata, shared pending-job file
  parsing/cleanup for web/touch/API conflict and display flows, shared macro
  names/transient-file filtering, shared web/API status label mapping,
  shared pending-job display-name fallback,
  shared manual-action safety gating,
  shared temperature-target readiness,
  shared print elapsed-time calculation,
  shared print progress calculation,
  shared UM API progress fraction clamping,
  shared job state-or-none naming,
  shared print completion history labeling,
  shared active/preparing/stoppable print-context decisions,
  touchscreen/web macro and G-code command helper routing,
  shared backend route diagnostics,
  touchscreen conflict actions and Cura cluster pending-job actions through
  native `JOB`/`ABORT` backend commands, native Cura upload registration and
  no-conflict `JOB` startup, reversible native-vs-stock print service init
  gating, low-volume side-by-side diagnostic logging, native
  frame-light/material-import/diagnostics UI helpers, native
  error mapping, and native diagnostics slices. It is still disabled by default
  and does not yet satisfy the release criteria for replacing stock
  `printserver`.
- The lab-only switch is explicit and reversible in the package scripts:
  `deneb.printsvc.enabled` defaults to `0`, existing lab values are preserved
  across Deneb updates, native `deneb-printsvc` stops stock `printserver` on
  start, and the patched stock `printserver` init skips `print_service.py` only
  while the native flag is `1`.
- Keep `onion-helper` under observation, but do not disable it yet. A live
  stop test showed SSH, Ethernet client networking, `udhcpc`, `deneb-ui`,
  `coordinator.py`, `print_service.py`, and the separate `onion` ubus API
  remained healthy. The daemon exposes generic ubus helper methods for process
  launch, file writes, and downloads, so it is more of an attack-surface
  candidate than a large RAM win: its latest sample shows about 1.2 MB RSS, but
  only about 100 KiB was anonymous/private memory.
- Remove or suppress stock Python `__pycache__` writes only if it does not hurt
  boot/runtime behavior; the latest overlay sample shows about 260 KiB of
  bytecode cache.
- Measure Cura workflows specifically: mDNS advertisement idle cost, repeated
  Cura monitor polling, multipart upload, pending conflict/preheat flow,
  pause/resume/abort actions, and cleanup after failed uploads.

Before treating a build as release-ready, repeat memory and CPU sampling while
idle, printing, installing a Deneb package, exporting diagnostics, switching
languages, and using Digital Factory pairing/disconnect.
