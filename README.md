# Deneb

Deneb is a community firmware-mod project for the UltiMaker 2+ Connect. The goal is to make the printer more local-first, more responsive, and less resource-taxed while preserving safety-critical behavior and a path back to official UltiMaker firmware.

This repository is private while the project is being organized.

## Current Status

Deneb is an experimental firmware modification, not yet a stable replacement
firmware or independent image.

- Native touchscreen, print-service, Digital Factory, and Web/API implementations
  exist and have partial target proof.
- The printer currently runs older package `1a4a4afe-dirty`; later source changes
  are not target-proven because the local WSL build environment is unavailable.
- Pause safety, material and leveling workflows, diagnostics, Web connection
  cleanup, long-soak behavior, and complete Python removal remain open.
- A current OpenWrt base, independent Deneb image, and modern Marlin port are
  planned work, not completed features.

Read [the documentation index](docs/README.md) first, then use
[the project dashboard](docs/PROJECT_STATUS.md) for the current done / in
progress / planned / blocked / broken view. Do not infer completion from an old
checklist, audit, or dated evidence file.

## Touchscreen UI

The stock Python/LVGL menu (33.7 MB VSZ in the stock baseline) has been replaced with a native LVGL v9 C implementation. The current measured Deneb UI process is about 2.7 MB VSZ / about 1.5-2 MB RSS at idle. The native UI release artifact was about 1.8 MiB before the web runtime was bundled, after adding stock-menu coverage and generated i18n fonts. Digital Factory UI actions now call the local `deneb-api digital-factory` command mode so the connector logic stays out of the UI binary without adding another packaged runtime. See [ui/README.md](ui/README.md) for build instructions and architecture details.

## Web UI

Deneb now includes a lightweight local web runtime: static HTML/CSS/JS served by lighttpd, with lighttpd proxying API requests to `deneb-api`; `deneb-api` talks directly to native `deneb-printsvc` over ZeroMQ in the native route. A small `deneb-mdns` service advertises `_ultimaker._tcp.local.` for Cura's local discovery browser. The implemented web surface covers status, current print job, temperatures, pause/resume/cancel/stop, manual heat/cooldown, and guarded X/Y/Z motion controls. Implementation does not imply that every Web workflow is hardware-proven.

The API includes UltiMaker REST API v1-shaped print/status/material endpoints and the single-printer `/cluster-api/v1/` endpoints Cura polls for discovery, monitor, upload, and basic print-job actions. Cura support currently requires the Deneb Cura plugin so the advertised `deneb_um2c` machine maps back to Cura's stock `ultimaker2_plus_connect` profile. A 2026-06-14 Cura 5.13 local-network run on package `ff49e86b` proved discovery, upload, material-mismatch Cancel, Continue/start, completion, pause/resume, cancel/abort back to idle, and pending mismatch recovery after UI/API/print-service restarts; package `9cdb5d6f` then proved S5-style progress/time reporting started at 0% with reasonable timer behavior. Broader failure-mode coverage and local-storage safety remain release blockers. See [docs/WEB_UI.md](docs/WEB_UI.md) and [docs/CURA_INTEGRATION.md](docs/CURA_INTEGRATION.md).

## Optimization Notes

Deneb's native services substantially reduce the active Python footprint, but
this does not mean Python can be removed from the base image. The legacy
squashfs, bootstrap/rollback paths, and AVR programming/recovery still contain
or require Python. Resource and de-Python work must be judged by current target
proof and full recovery coverage, not package contents alone.

The next Web resource experiment is direct static serving from `deneb-api`, but
lighttpd must remain available for comparison and rollback until concurrency,
upload, SSE, malformed-client, restart, and memory tests pass. See the
[project dashboard](docs/PROJECT_STATUS.md) and
[modernization roadmap](docs/PLATFORM_MODERNIZATION_ROADMAP.md).

## Documentation

Start with [docs/README.md](docs/README.md). Common entry points are:

- [Current project status](docs/PROJECT_STATUS.md)
- [Modernization roadmap](docs/PLATFORM_MODERNIZATION_ROADMAP.md)
- [WSL build environment](docs/WSL_BUILD_ENVIRONMENT.md)
- [WiFi setup via USB](docs/WIFI_SETUP.md)
- [Ethernet setup via USB](docs/ETH_SETUP.md)
- [Web UI and API](docs/WEB_UI.md)
- [Cura integration](docs/CURA_INTEGRATION.md)
- [Backend IPC protocol](docs/BACKEND_IPC_PROTOCOL.md)

Historical investigations and completed plans are indexed under `docs/evidence/`
and `docs/archive/`; they are not current status pages.
## Build And Verification

Deneb C targets are Linux/POSIX-oriented. Do not use Visual Studio/MSVC CMake
generators for `ui` or `web`; they are intentionally unsupported. From Windows,
run host verification through WSL so the same POSIX headers and toolchain shape
are used as the target-side code expects.

Fresh workstations must complete the root-default Debian, MIPS musl toolchain,
mbedTLS, ZeroMQ, and lighttpd bootstrap documented in
[docs/WSL_BUILD_ENVIRONMENT.md](docs/WSL_BUILD_ENVIRONMENT.md). A registered
Debian distribution by itself is not a complete Deneb build environment.

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
