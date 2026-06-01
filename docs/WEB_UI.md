# Deneb Web UI

A lightweight web interface for the UltiMaker 2+ Connect running Deneb firmware. Implements the **UltiMaker REST API v1** for Cura LAN printing compatibility.

## Architecture

```
Browser/Cura  --->  lighttpd (:80)  --->  deneb-api (Unix socket)
                       |                         |
                  static files              ZMQ SUB :5565
                  /www/deneb/              ZMQ REQ :5566
```

- **lighttpd** serves static files and reverse-proxies `/api/v1/*` to deneb-api
- **deneb-api** implements the UltiMaker REST API v1, connecting to the stock coordinator via ZeroMQ
- **Static web UI** is vanilla HTML/CSS/JS (~25 KB), no frameworks

## Resource Budget

| Resource | Target |
|----------|--------|
| deneb-api RSS | < 1.5 MB |
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

Implements the UltiMaker REST API v1 (same as S3/S5/S7 printers):
- `GET /api/v1/printer/status` — printer status string
- `GET /api/v1/printer` — full printer state
- `GET /api/v1/print_job` — current print job
- `GET /api/v1/system` — system information
- `POST /api/v1/print_job` — upload and start print (M7)
- `PUT /api/v1/print_job/state` — pause/resume/cancel (M7)

Cura can discover the printer and, when the web UI is configured for Open
Access, upload prints over LAN. Password-protected web UI mode does not expose
the unauthenticated UltiMaker pairing flow; use the Deneb web UI for protected
LAN control.

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
uci commit deneb
```

## Files

| Path | Description |
|------|-------------|
| `/usr/bin/deneb-api` | API server binary |
| `/usr/sbin/lighttpd` | Web server binary |
| `/etc/deneb/lighttpd.conf` | lighttpd configuration |
| `/etc/deneb/web-auth.conf` | Auth configuration |
| `/www/deneb/` | Static web assets |
| `/var/run/deneb-api.sock` | Unix domain socket |
