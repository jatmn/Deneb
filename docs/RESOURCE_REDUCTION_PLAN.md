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
- Map `print_service.py` replacement requirements before any implementation
  work. The live investigation shows it owns `/dev/ttyS1`, verifies/programs
  the motion-controller firmware at startup, handles job/macro/G-code control,
  publishes live status on ZMQ, and implements Marlin flow control, CRC/framing,
  resend handling, and pause/resume state. It is a meaningful RAM target, but
  high risk.
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

Before treating a build as release-ready, repeat memory and CPU sampling while
idle, printing, installing a Deneb package, exporting diagnostics, switching
languages, and using Digital Factory pairing/disconnect.
