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
  `api_cluster`, `api_printer`, conflict/preheat action bridges, pending-job
  metadata, direct macro calls, raw G-code calls, and duplicated status
  classifiers all need an ownership decision: keep as clients, move into
  `deneb-printsvc`, or replace with a shared Deneb print-control API.
- Do not preserve awkward compatibility layers just because they match the
  current Python driver's shape. Any shim kept for migration needs explicit
  removal criteria and tests proving the final Deneb-owned contract is cleaner
  for web UI, touchscreen UI, API, and Cura LAN flows.
- Deduplicate print-control behavior while the native service is introduced.
  Status classification, print/pending job metadata, command formatting, macro
  lookup, safe motion policy, heat-state decisions, pause/resume/abort
  semantics, and error mapping should each have one owner. Prefer shared native
  helpers or a single `deneb-printsvc` API over copy-pasted logic between LCD
  UI, web UI, REST API, Cura cluster API, and diagnostics.
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
- The native replacement now has an initial buildable C source tree at
  `printsvc/`, is included in release packages as a lab-gated binary, and has
  host tests for the first command/status/packet/flow-control/heater-wait
  slices. It is still disabled by default and does not yet satisfy the release
  criteria for replacing stock `printserver`.
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
