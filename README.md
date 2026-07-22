# Deneb

Deneb is a local-first, resource-conscious firmware mod for the UltiMaker 2+
Connect. It replaces key Python services with small native components while
preserving hardware safety and a path back to official firmware.

> Experimental: not yet a stable replacement firmware or independent image.

## Current Status

| Area | Status | Key gap |
| --- | --- | --- |
| Touchscreen | **Experimental** | Native LVGL UI works; Pause safety, material, leveling Cancel, update UX, and diagnostics remain open. |
| Print service | **Experimental** | Native completion and abort are proven in bounded tests; Pause/Resume and long-soak stability are not release-ready. |
| Web/API/Cura | **MVP** | Local control, upload, and Cura discovery exist; connection cleanup, storage UX, security, and failure recovery need work. |
| Python removal | **In progress** | Native services replace the active stack, but the base image, rollback, and AVR recovery still require Python. |
| Resource optimization | **In progress** | Retain lighttpd as the HTTP front end and move more generic HTTP work into it where that improves reliability without exceeding resource limits. |
| Independent image and modern Marlin | **Planned** | Current OpenWrt hardware support, safe recovery, and the controller port are not complete. |

See [Project Status](docs/PROJECT_STATUS.md) for the detailed work board and
[Modernization Roadmap](docs/PLATFORM_MODERNIZATION_ROADMAP.md) for phase gates.

## Components

| Component | Role |
| --- | --- |
| `deneb-ui` | Native LVGL touchscreen interface |
| `deneb-printsvc` | Native print backend |
| `deneb-api` and static Web UI | Local REST, print control, status, and browser interface |
| `deneb-mdns` and Cura plugin | Cura discovery and UM2+ Connect profile mapping |
| USB network setup | Client-only WiFi and Ethernet configuration |
| `.deneb` update package | Installs and audits the native stack |

Implementation does not imply that every workflow is hardware-proven.

## Documentation

| Need | Document |
| --- | --- |
| Documentation map | [docs/README.md](docs/README.md) |
| Current work, defects, and priorities | [Project Status](docs/PROJECT_STATUS.md) |
| De-Python, Web, OpenWrt, image, and Marlin plan | [Modernization Roadmap](docs/PLATFORM_MODERNIZATION_ROADMAP.md) |
| Windows/WSL cross-build setup | [WSL Build Environment](docs/WSL_BUILD_ENVIRONMENT.md) |
| Web/API and Cura | [Web UI](docs/WEB_UI.md) / [Cura](docs/CURA_INTEGRATION.md) |
| Third-party slicer command/profile rules | [Slicer Compatibility](docs/SLICER_COMPATIBILITY.md) |
| Touchscreen and backend | [UI](ui/README.md) / [IPC](docs/BACKEND_IPC_PROTOCOL.md) |
| Contribution and source provenance | [CONTRIBUTING.md](CONTRIBUTING.md) / [Provenance](docs/SOURCE_PROVENANCE.md) |

Dated investigations are under `docs/evidence/`; superseded plans are under
`docs/archive/`. Neither is a current work queue.

## Build

Target binaries require Debian under WSL 2 and the documented MIPS musl
toolchain. Complete the [WSL setup](docs/WSL_BUILD_ENVIRONMENT.md) first.

```powershell
# Experimental MIPS update package
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1

# Touchscreen host-stub build
powershell -ExecutionPolicy Bypass -File tools/build-ui-host.ps1
```

A package is valid only when the release wrapper's audits finish successfully.

## Safety and Repository Boundary

Deneb controls motion, heating, networking, and updates. Treat changes as
hardware-affecting until target-proven, and never expose control endpoints to
untrusted networks.

This repository may contain original Deneb code, documentation, build/install
scripts, and compatibility layers. It must not contain proprietary firmware or
extracted filesystems, decompiled source, private keys, device secrets, or
modified firmware images.

## License

Deneb is an independent community project and is not endorsed by UltiMaker.
Original Deneb files are intended to use MPL-2.0 unless stated otherwise. See
[LICENSE](LICENSE), [COMPLIANCE.md](COMPLIANCE.md), [source provenance](docs/SOURCE_PROVENANCE.md), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
