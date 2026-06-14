# Deneb

Deneb is a community firmware-mod project for the UltiMaker 2+ Connect. The goal is to make the printer more local-first, more responsive, and less resource-taxed while preserving safety-critical behavior and a path back to official UltiMaker firmware.

This repository is private while the project is being organized.

## Current Status

### Completed
- [x] SSH bootstrap package (enables SSH access for development)
- [x] Branding assets (splash screen, icons)
- [x] Early-boot framebuffer splash (S11, raw RGB565 to /dev/fb0)
- [x] Touchscreen UI replacement (LVGL v9 C, replaces Python/Cygnus menu)
- [x] Backend IPC integration (ZeroMQ client for coordinator)
- [x] Client-only network setup via USB `wifi.txt` / `eth.txt`
- [x] Stock WiFi AP/captive-portal web setup disabled and hidden from the live filesystem view
- [x] AP-side DHCP/DNS/IPv6 server services disabled for Deneb installs
- [x] Lightweight local web UI and `deneb-api` runtime bundled into `.deneb` update releases
- [x] Web status, pause/resume/cancel, manual heat, cooldown, and X/Y/Z jog controls
- [x] Cura mDNS advertisement (`deneb-mdns`) and local cluster API compatibility surface
- [x] Deneb Cura network discovery plugin package for mapping `deneb_um2c` to Cura's stock UM2+ Connect profile
- [x] Cura/cluster upload-start path with pending-job metadata and conflict continue/cancel bridges
- [x] Touchscreen print-state fixes for boot idle Stop state, preheat Stop availability, mismatch continue flow, and abort cleanup/status handling
- [x] Experimental native `deneb-printsvc` replacement for the stock Python `marlindriver` print service, packaged with native-only route and no-Python archive gates
- [x] Live device inspection (process list, memory, IPC ports, macros)
- [x] Baseline measurements documented
- [x] Initial on-device Deneb UI resource measurements

### In Progress
- [ ] Broader release testing of the new UI on hardware
- [ ] Web/API validation on hardware, including resource behavior while serving status and controls
- [ ] RAM/CPU benchmark comparisons while printing, uploading, updating, and exporting diagnostics
- [ ] `.deneb` package manifest, rollback, signing, and release-channel hardening
- [ ] Cura discovery/upload/start compatibility validation against current Cura builds and real printer behavior
- [ ] Native `deneb-printsvc` promotion gates: LCD/Web hands-on flows, desktop Cura, Digital Factory lifecycle, representative slicer output, and long active-soak memory behavior

### Planned
- [ ] Generic slicer G-code support
- [ ] OS/service modernization
- [ ] Marlin firmware work

## Priorities

1. Keep the public/project repo legally clean.
2. Keep the touchscreen and web UI replacements hardware-tested and resource-measured.
3. Close package rollback/signing gaps before calling `.deneb` releases stable.
4. Expand Cura, local-storage printing, and slicer compatibility only when the hardware behavior can be tested safely.

## Touchscreen UI

The stock Python/LVGL menu (33.7 MB VSZ in the stock baseline) has been replaced with a native LVGL v9 C implementation. The current measured Deneb UI process is about 2.7 MB VSZ / about 1.5-2 MB RSS at idle. The native UI release artifact was about 1.8 MiB before the web runtime was bundled, after adding stock-menu coverage and generated i18n fonts. Digital Factory UI actions now call the local `deneb-api digital-factory` command mode so the connector logic stays out of the UI binary without adding another packaged runtime. See [ui/README.md](ui/README.md) for build instructions and architecture details.

## Web UI

Deneb now includes a lightweight local web runtime: static HTML/CSS/JS served by lighttpd, with `deneb-api` proxying status and controls to the stock coordinator over ZeroMQ. A small `deneb-mdns` service advertises `_ultimaker._tcp.local.` for Cura's local discovery browser. The implemented web surface covers status, current print job, temperatures, pause/resume/cancel/stop, manual heat/cooldown, and guarded X/Y/Z motion controls.

The API includes UltiMaker REST API v1-shaped print/status/material endpoints and the single-printer `/cluster-api/v1/` endpoints Cura polls for discovery, monitor, upload, and basic print-job actions. Cura support currently requires the Deneb Cura plugin so the advertised `deneb_um2c` machine maps back to Cura's stock `ultimaker2_plus_connect` profile. A 2026-06-14 Cura 5.13 local-network run proved discovery, upload, material-mismatch Cancel, Continue/start, and completion on package `ff49e86b`; Cura pause/resume, abort/delete, progress/time reporting, stale-state recovery, and local-storage safety remain release blockers. See [docs/WEB_UI.md](docs/WEB_UI.md) and [docs/CURA_INTEGRATION.md](docs/CURA_INTEGRATION.md).

## Optimization Notes

Deneb's current optimization work is focused on removing dormant stock UI and setup paths while moving print control onto original native services. The `printsvc/` tree now provides the experimental native `deneb-printsvc` path for the stock Python `marlindriver` service. Bounded hardware evidence now covers native route ownership, generated heat/preheat and abort slices, pause/resume, completion, short repeated-job stability, and a strict bounded stock/native resource comparison. Remaining release blockers are the user-facing LCD/Web/Cura/Digital Factory workflows, representative slicer-output proof, and long active-soak memory behavior.

- The stock touchscreen UI no longer runs; Deneb starts the native LVGL UI instead.
- The installer prunes the dormant stock Python touchscreen files after a successful Deneb UI smoke test.
- WiFi setup is client-only through USB import. The old AP/captive-portal flow, Tornado `wificonnect` server, nodogsplash htdocs, and stock React WiFi setup assets are hidden from the live filesystem view.
- AP-side DHCP/DNS/IPv6 server services (`dnsmasq` and `odhcpd`) are stopped and disabled; the stock binaries remain in the read-only base image, but they no longer run.
- `.deneb` packages do not rebuild the vendor squashfs/rootfs. Cleanup hides stock base-image files with overlayfs whiteouts where needed, but reclaiming read-only base-image flash is intentionally outside the clean repository boundary.

## Device Setup Docs

- [WiFi setup via USB](docs/WIFI_SETUP.md): configure WiFi by placing `wifi.txt` on a USB drive and importing it from Settings > Network.
- [Ethernet setup via USB](docs/ETH_SETUP.md): configure static Ethernet or reset Ethernet to DHCP with `eth.txt`.
- [Web UI](docs/WEB_UI.md): local web status and controls plus the current API compatibility surface.
- [Cura integration](docs/CURA_INTEGRATION.md): mDNS, Cura plugin, cluster API, upload/start behavior, and remaining validation blockers.
- [Backend IPC protocol](docs/BACKEND_IPC_PROTOCOL.md): coordinator command/status protocol used by the native UI.
- [Stock UI coverage](docs/STOCK_UI_COVERAGE.md): current stock-vs-Deneb touchscreen feature parity.
- [Resource reduction plan](docs/RESOURCE_REDUCTION_PLAN.md): RAM, CPU, and release guardrails.
- [Native print service](printsvc/README.md): build, Valgrind, package, and hardware smoke validation.

## Build And Verification

Deneb C targets are Linux/POSIX-oriented. Do not use Visual Studio/MSVC CMake
generators for `ui` or `web`; they are intentionally unsupported. From Windows,
run host verification through WSL so the same POSIX headers and toolchain shape
are used as the target-side code expects.

```powershell
# Web/API host-stub verification
$winRepo = (Get-Location).Path.Replace('\', '/')
$repo = (wsl -- wslpath -a "$winRepo").Trim()
wsl -- bash -lc "cd '$repo' && cmake -S web -B /tmp/deneb-web-build -DBUILD_HOST_STUB=ON && cmake --build /tmp/deneb-web-build"

# Touchscreen UI host-stub verification
powershell -ExecutionPolicy Bypass -File tools/build-ui-host.ps1
```

## Project Boundary

This repository must not contain UltiMaker firmware images, extracted root filesystems, proprietary application source, recovered/decompiled vendor code, private keys, device secrets, or generated modified firmware images.

Users/developers must supply their own firmware image or printer for local analysis. Deneb should publish original code, documentation, build scripts, installer scripts, compatibility layers, and minimal patch/transformation logic only where needed.

## Name

The stock firmware/application layer appears to use the codename `Cygnus`. Deneb is the brightest star in Cygnus. The name keeps a respectful relationship to that lineage while making clear this is a separate community mod, not official UltiMaker firmware.

## License

Original Deneb project files are intended to be licensed under MPL-2.0 unless a file or component says otherwise. This license does not apply to UltiMaker firmware, extracted firmware files, user-supplied device files, or third-party components with their own licenses.

See [LICENSE](LICENSE), [COMPLIANCE.md](COMPLIANCE.md), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Safety

This project can affect printer networking, motion, heating, updates, and diagnostics. Treat every change as hardware-affecting until proven otherwise. Do not expose unauthenticated heat, motion, print, or raw G-code controls on untrusted networks.
