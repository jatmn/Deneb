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

- **lighttpd** serves static files and reverse-proxies `/api/v1/*` to deneb-api
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

`deneb-mdns` advertises `_ultimaker._tcp.local.` with `type=printer`, the
printer name/address, firmware version, and a configurable `machine` TXT value.
The default `machine` value is `ultimaker2_plus_connect`; if a specific Cura
build requires a BOM-style value, set `deneb.mdns.machine` through UCI and
restart `deneb-mdns`.

This is not yet a completed Cura upload compatibility claim. Any required
`/cluster-api/v1/` behavior and validation against current Cura behavior on real
hardware remain open. Password-protected web UI mode does not expose the
unauthenticated UltiMaker pairing flow; use the Deneb web UI for protected LAN
control.

## Current Limits

- Web/API resource numbers still need hardware measurement while idle, polling,
  uploading, printing, and updating.
- Upload/start behavior exists in `deneb-api`, but local storage behavior,
  free-space checks, USB-removal-safe printing, and real Cura upload/start
  testing remain release blockers.
- Cura discovery depends on the Cura version's handling of UM2+ Connect local
  network printers. The mDNS layer is present, but the advertised `machine`
  value may need hardware/Cura-version validation.
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
uci set deneb.mdns.machine=ultimaker2_plus_connect
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
