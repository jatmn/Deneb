# Deneb Web UI

A lightweight web interface for the UltiMaker 2+ Connect running Deneb firmware. It provides local status and controls plus an initial **UltiMaker REST API v1**-shaped surface intended for future Cura LAN printing compatibility.

## Architecture

```
Browser/Cura  --->  lighttpd (:80)  --->  deneb-api (Unix socket)
     |                 |                         |
     |            static files              ZMQ SUB :5565
     |            /www/deneb/              ZMQ REQ :5566
     |
     +-------->  deneb-mdns (:5353 UDP, _ultimaker._tcp.local.)
```

- **lighttpd** serves static files and reverse-proxies `/api/v1/*` and `/cluster-api/v1/*` to deneb-api
- **deneb-api** implements the UltiMaker REST API v1, connecting to the stock coordinator via ZeroMQ
- **deneb-mdns** advertises the printer on `_ultimaker._tcp.local.` for Cura's local discovery browser
- **Static web UI** is vanilla HTML/CSS/JS (~25 KB), no frameworks

## Resource Budget

| Resource | Target |
|----------|--------|
| deneb-api RSS | < 1.5 MB |
| deneb-mdns RSS | < 512 KB |
| lighttpd RSS | < 1 MB |
| Static assets | < 50 KB |
| **Total** | **< 4 MB** |

## Installation

1. Build the update package: `tools/build-update-release.ps1`
2. Copy `dist/Deneb_Update_<version>.deneb` to a USB drive
3. On the printer: Settings > Update Firmware > Select the .deneb file
4. The web UI is available at `http://<printer-ip>/`

## First-Connect Setup

On first access, the web UI shows a setup page:
- Set a password (recommended)
- Or choose "Open Access" (no password required)

## API Compatibility

Implemented local API surface:
- `GET /api/v1/printer/status` — printer status string
- `GET /api/v1/printer` — full printer state
- `GET /api/v1/print_job` — current print job
- `GET /api/v1/system` — system information
- `POST /api/v1/print_job` — initial multipart upload and start handler
- `PUT /api/v1/print_job/state` — pause/resume/cancel
- `GET /cluster-api/v1/printers` — Cura local-network single-printer status
- `GET /cluster-api/v1/print_jobs` — Cura local-network active print job status
- `POST /cluster-api/v1/print_jobs/` — Cura local-network multipart upload/start handler; requires Open Access or Deneb auth
- `PUT /cluster-api/v1/print_jobs/{uuid}/action` — Cura pause/resume/abort bridge; requires Open Access or Deneb auth

`deneb-mdns` advertises `_ultimaker._tcp.local.` with `type=printer`, the
printer name/address, firmware version, and a configurable `machine` TXT value.
Current Cura derives the displayed network printer model by matching this value
against BOM numbers in its bundled machine definitions. The UM2+ Connect
definition is network-capable but does not currently publish a BOM number. Deneb
therefore advertises `deneb_um2c`. Cura support for that id must come from the
Deneb Cura plugin in `cura/plugins/DenebUM2CNetworkPrinting`, which contributes
a plugin-owned definition that inherits the stock UM2+ Connect profile and maps
Deneb discovery back to UM2+ Connect geometry, materials, and quality settings.
Do not patch Cura's bundled resources or copy definition files into Cura's user
profile directly.

Build the Cura plugin package with:
```powershell
powershell -ExecutionPolicy Bypass -File tools/build-cura-plugin.ps1
```

Install `dist/DenebUM2CNetworkPrinting.curapackage` through Cura's package
install flow, then restart Cura so the plugin can register its resources before
network discovery loads machine metadata.

The Cura local-network path implements the single-printer cluster endpoints that
Cura 5.13 polls for monitor, upload, and basic print-job actions. Stock Cura
does not send Deneb session credentials on these cluster write requests, so Cura
LAN upload/control currently requires Open Access mode. In password-protected
mode, use the Deneb web UI for protected LAN control until Deneb implements a
Cura-compatible pairing/auth flow. Validation against current Cura behavior on
real hardware remains open.

## Current Limits

- Web/API resource numbers still need hardware measurement while idle, polling,
  uploading, printing, and updating.
- Upload/start behavior exists in `deneb-api` for both UM API v1 and Cura's
  local cluster API, but local storage behavior, free-space checks,
  USB-removal-safe printing, and real Cura upload/start testing remain release
  blockers.
- Cura discovery requires installing the Deneb Cura plugin until Cura exposes a
  BOM-backed UM2+ Connect local network definition.
- Motion controls are intentionally limited to guarded X/Y/Z movement; extruder
  jogging is not exposed until safe-temperature gating is designed and tested.

## Deneb Extensions

- `GET /api/v1/deneb/events` — SSE stream (1 Hz status updates)
- `GET /api/v1/deneb/version` — Deneb version info
- `POST /api/v1/deneb/setup` — First-connect setup
- `POST /api/v1/deneb/auth` — Session authentication

## Configuration

Web UI is opt-in via UCI:
```sh
uci set deneb.web.enabled=1  # Enable
uci set deneb.web.enabled=0  # Disable
uci set deneb.mdns.enabled=1 # Enable Cura/mDNS advertisement
uci set deneb.mdns.machine=deneb_um2c
uci commit deneb
```

## Files

| Path | Description |
|------|-------------|
| `/usr/bin/deneb-api` | API server binary |
| `/usr/bin/deneb-mdns` | Cura/mDNS advertiser binary |
| `/usr/sbin/lighttpd` | Web server binary |
| `/etc/deneb/lighttpd.conf` | lighttpd configuration |
| `/etc/deneb/web-auth.conf` | Auth configuration |
| `/www/deneb/` | Static web assets |
| `/var/run/deneb-api.sock` | Unix domain socket |
