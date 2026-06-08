# Deneb Web UI

A lightweight web interface for the UltiMaker 2+ Connect running Deneb firmware. It provides local status and controls plus UltiMaker REST API v1-shaped and Cura local cluster API compatibility surfaces.

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

For the Cura-specific discovery, plugin, and upload/start behavior, see
[Cura integration](CURA_INTEGRATION.md).

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
- `GET /cluster-api/v1/materials` and `POST /cluster-api/v1/materials` — Cura material compatibility data/import
- `GET /cluster-api/v1/printers` — Cura local-network single-printer status
- `GET /cluster-api/v1/print_jobs` — Cura local-network active or pending print job status
- `POST /cluster-api/v1/print_jobs` and `POST /cluster-api/v1/print_jobs/` — Cura local-network multipart upload/start handler; intentionally unauthenticated for stock Cura compatibility
- `PUT /cluster-api/v1/print_jobs/{uuid}/action` — Cura pause/resume/abort/print bridge; intentionally unauthenticated for stock Cura compatibility
- `DELETE /cluster-api/v1/print_jobs/{uuid}` — Cura abort/delete bridge
- `GET /cluster-api/v1/print_jobs/{uuid}/preview` — preview placeholder endpoint

`deneb-mdns` advertises `_ultimaker._tcp.local.` with `type=printer`, the
printer name/address, a configurable `machine` TXT value, and a Cura Connect
compatibility `firmware_version`. Cura rejects local-network devices whose
advertised firmware version is older than `4.0.0`, so Deneb defaults the mDNS
`firmware_version` to `4.0.0` instead of the Deneb git build version. The real
Deneb build version remains available from `/api/v1/deneb/version`.
The selected print backend route is available from
`/api/v1/deneb/print_backend`; it returns the shared native route helper's
backend name plus status and command endpoint URLs for lab validation.

Current Cura derives the displayed network printer model by matching the mDNS
`machine` value against BOM numbers in its bundled machine definitions. The
UM2+ Connect definition is network-capable but does not currently publish a BOM
number. Deneb therefore advertises `deneb_um2c`. Cura support for that id must
come from the Deneb Cura plugin in `cura/plugins/DenebUM2CNetworkPrinting`,
which patches Cura's local network discovery mapping so `deneb_um2c` resolves
to the stock `ultimaker2_plus_connect` machine id. This preserves stock UM2+
Connect materials, variants, quality profiles, and printer presets. Do not
patch Cura's bundled resources or copy definition files into Cura's user
profile directly.

Build the Cura plugin package with:
```powershell
powershell -ExecutionPolicy Bypass -File tools/build-cura-plugin.ps1
```

Install `dist/DenebUM2CNetworkPrinting.curapackage` through Cura's package
install flow, then restart Cura so the plugin can register its resources before
network discovery loads machine metadata.

The Cura local-network path implements the single-printer cluster endpoints that
Cura 5.13 polls for monitor, upload, materials, and basic print-job actions.
Stock Cura does not send Deneb session credentials on these cluster write
requests, so Deneb accepts cluster upload/control without Deneb auth to preserve
stock Cura LAN behavior. The UM API v1 write endpoints and Deneb web UI remain
protected by Open Access or Deneb auth. Validation against current Cura behavior
on real hardware remains open.

## Current Limits

- Web/API resource numbers still need hardware measurement while idle, polling,
  uploading, printing, and updating.
- Upload/start behavior exists in `deneb-api` for both UM API v1 and Cura's
  local cluster API, but local storage behavior, free-space checks,
  USB-removal-safe printing, and real Cura upload/start testing remain release
  blockers.
- Conflict continue/cancel and pending preheat visibility now use native Deneb
  pending-job and print-state helpers instead of embedded Python coordinator
  launchers. Web motion macros, Cura upload start, and pending-job continue now
  route through native backend macro/job helpers instead of local command JSON.
  Cura upload registration also delegates pending-vs-immediate-start dispatch
  sequencing to the native pending-job registration helper, leaving the web
  layer responsible only for backend transport callbacks.
  When `deneb.printsvc.enabled=1`, `deneb-api` selects native `deneb-printsvc`
  status/command ports directly instead of routing print-service traffic
  through the Python coordinator; the coordinator route remains the default
  fallback while the native service is lab-gated. The cached Deneb status JSON
  exposes `print_backend`, `print_backend_status_url`, and
  `print_backend_command_url` from the shared native route helper so lab checks
  can confirm which backend the API selected.
  Web motion macro names and pending-job display reads are shared with the
  native print-control helpers instead of being hard-coded/parsing JSON in this
  layer.
  Cura upload storage preparation uses the shared native print-file helper for
  sanitized filenames and Deneb spool destinations before web/API pending-job
  dedupe, native registration, and response formatting.
  Multipart upload extraction is also isolated in `web/src/api_multipart.c`,
  shared by Cura print uploads and material uploads instead of living in the
  HTTP server main loop.
  Cura material uploads then hand the extracted file to the shared native
  material catalog helper, which owns default catalog persistence and temp-file
  cleanup.
  UM API and Deneb API current-job responses now use shared native print-job
  summary formatters, keeping active-job escaping, elapsed time, state, and
  progress semantics out of endpoint-local JSON assembly. UM API current-job
  scalar endpoints use the same native formatter owner for string escaping and
  numeric response bodies.
  UM printer root/status responses now delegate their top-level status fields,
  temperature/position shape, and pending filename fallback to
  `common/print/printer_status_response.*`. UM printer bed/head/extruder,
  hotend, position, and feeder subresources use the same shared formatter
  instead of endpoint-local JSON fragments. Material, LED, ambient, and Air
  Manager compatibility responses now use the same native printer response
  owner, including LED scalar defaults. `backend_zmq` now exposes a printer
  status response snapshot for these endpoints so `api_printer` no longer maps
  cached backend fields itself.
  Web temperature writes also delegate bounded target parsing and `M104`/`M140`
  command planning to the shared native G-code helper instead of clamping and
  formatting heater commands inside the REST endpoint.
  Web position writes now delegate jog/absolute-position JSON parsing,
  build-volume checks, speed checks, and raw move-command planning to that same
  helper before the endpoint sends the planned command sequence.
  Web motion action writes also delegate action JSON parsing, legacy home
  fallback, and macro-vs-G-code selection to the shared manual-motion helper.
  Cura cluster action writes use the shared print-state action parser for the
  pending-job `print` default, keeping that compatibility behavior out of the
  endpoint.
  Cura cluster active-job responses use the shared native print-job summary
  formatter for job metadata, printer assignment, build plate, configuration,
  and compatible-machine-family fields.
  UM API and cluster API status labels also share the native print-state helper
  instead of each REST surface owning its own active-job mapping.
  Remaining native-service work should keep collapsing web/API and touchscreen
  behavior toward shared print-control helpers instead of adding new per-client
  state rules.
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
uci set deneb.mdns.firmware=4.0.0
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
