# Deneb Web UI

A lightweight web interface for the UltiMaker 2+ Connect running Deneb firmware. It provides local status and controls plus UltiMaker REST API v1-shaped and Cura local cluster API compatibility surfaces.

Status reconciliation: 2026-07-10. The implementation is an MVP, not yet a
first-class or release-complete experience. Dated target proof exists for live
status/progress and Cura 5.13 workflows, while Web pause/resume/cancel,
stale-state recovery, upload/storage failure UX, and the latest native Pause
mitigation remain open. The 2026-07-10 resource run also confirmed SSE
connection starvation and post-disconnect descriptor retention.

## Architecture

```
Browser/Cura  --->  lighttpd (:80)  --->  deneb-api (Unix socket)
     |                 |                         |
     |            static files              ZMQ SUB :5555
     |            /www/deneb/              ZMQ REQ :5556
     |
     +-------->  deneb-mdns (:5353 UDP, _ultimaker._tcp.local.)
```

- **lighttpd** serves static files and reverse-proxies `/api/v1/*` and `/cluster-api/v1/*` to deneb-api
- **deneb-api** implements the UltiMaker REST API v1, connecting to native `deneb-printsvc` via ZeroMQ by default
- **deneb-mdns** advertises the printer on `_ultimaker._tcp.local.` for Cura's local discovery browser
- **Static web UI** is vanilla HTML/CSS/JS (115.7 KiB tracked on 2026-07-22), no frameworks

For the Cura-specific discovery, plugin, and upload/start behavior, see
[Cura integration](CURA_INTEGRATION.md).

## Current Resource Baseline

| Resource | Current observation |
| --- | --- |
| deneb-api RSS | 0.9-1.0 MiB on target, 2026-07-10 |
| deneb-mdns RSS | 0.12 MiB on target, 2026-07-10 |
| lighttpd RSS | 0.44 MiB on target, 2026-07-10 |
| Static assets | 115.7 KiB tracked in web/www, 2026-07-22 |

The table above records current measurements and working thresholds; it is not a release limiter or a promise that every component must remain under a fixed cap. Changes still require total memory, CPU, storage, and connection measurements because the target is severely constrained. On 2026-07-10, direct `/proc` RSS
samples after clean boot were approximately 0.9-1.0 MiB for `deneb-api`, 0.44
MiB for lighttpd, and 0.12 MiB for `deneb-mdns`. Three SSE clients plus 120 REST
requests completed with flat 1,032 KiB API RSS. Four SSE clients starved REST
past a 90-second bound and left seven extra API/proxy descriptors until reboot.
See [the dated target report](evidence/TARGET_AUTOMATION_2026-07-10.md). CPU, upload,
slow-client, and longer-duration matrices remain required.

## First-Class Experience And HTTP/Application Boundary

lighttpd is part of Deneb's added Web stack; the original firmware did not have
this Web UI. It remains the supported HTTP front end because it is small,
mature, and isolates network-facing HTTP behavior from printer-control logic.
Direct static serving from `deneb-api` and removal of lighttpd are not project
goals.

> **Boundary:** lighttpd owns HTTP transport and edge behavior. `deneb-api` owns
> printer/application behavior. Printer state, safety policy, Cura semantics,
> job decisions, and command execution must never move into lighttpd.

The current `deneb-api` is both a small custom HTTP application server and the
printer API implementation. Its route table contains about 80 UM API, Deneb,
and Cura endpoints. It also owns printer-state translation, safety validation,
print actions, ZMQ communication, upload registration, session/auth behavior,
SSE event generation, system data, and Digital Factory commands. lighttpd
cannot replace those application responsibilities without another dynamic
backend.

### Responsibility decision

| Responsibility | Direction |
| --- | --- |
| Static files, MIME types, index handling | **lighttpd**; add cache validators and immutable caching for versioned assets |
| HTTP connection handling, keep-alive, headers, limits, timeouts, slow clients | **lighttpd** wherever supported and measured |
| Security headers and sensitive-path denial | **lighttpd** |
| Maintenance/degraded page while the API restarts | **lighttpd** |
| TLS termination and bounded access logging | **Evaluate in lighttpd**; current static build excludes TLS and access-log modules, so flash/RAM/CPU and SD-write cost must be measured |
| Static locale/version assets | **Candidate for lighttpd** when responses can be generated at install/build time without becoming stale |
| Raw HTTP parsing and response framing inside `deneb-api` | **Evaluate FastCGI/SCGI-style offload** so lighttpd owns transport lifecycle; current build includes only the proxy path |
| SSE transport | **Shared boundary**: lighttpd owns the client/proxy connection; `deneb-api` generates printer events and must close disconnected backend sockets correctly |
| Upload ingress | **Shared boundary**: lighttpd enforces HTTP limits/timeouts and streams; `deneb-api` validates multipart content, storage, job conflicts, and print registration |
| Authentication | **Application-owned by default** because setup, bearer sessions, Digest compatibility, Open Access, and intentionally unauthenticated Cura routes are policy-sensitive; optional edge authentication can only be defense in depth |
| Printer/Cura JSON, safety rules, print actions, ZMQ, pending jobs | **Keep in `deneb-api` or shared native print libraries** |

The packaged lighttpd 1.4.76 build currently includes only `mod_access`,
`mod_alias`, `mod_indexfile`, `mod_staticfile`, `mod_setenv`, and `mod_proxy`.
Adding authentication, access logging, TLS, FastCGI/SCGI, compression, or other
modules is an explicit resource and security decision, not a free capability.

The largest plausible offload is replacing the custom raw-HTTP Unix-socket
transport in `deneb-api` with a lighttpd application protocol such as FastCGI or
SCGI. This could remove custom accept/keep-alive/body/response lifecycle code,
but it would not remove the API application process. Adopt it only if a focused
prototype shows lower total complexity and stable RSS while preserving
streaming uploads, SSE, Cura compatibility, and recovery behavior.

### Current transport findings

| Finding | Implication |
| --- | --- |
| lighttpd `server.max-connections` is 8 and the API `MAX_SSE_CLIENTS` is 4 | The limits and proxy/backend connection accounting must be tested together; do not merely raise limits without resource and denial-of-service bounds. |
| SSE sockets are registered for `EPOLLHUP | EPOLLERR`, but the main loop only handles ordinary client `EPOLLIN` | This is a likely cause of retained backend descriptors after disconnect and should be fixed before any protocol migration. |
| Multipart upload reads synchronously in the single API event loop and may poll for up to 30 seconds | A slow/interrupted upload can delay unrelated API work; lighttpd transport limits plus a nonblocking or FastCGI/SCGI body path are worth prototyping. |
| `deneb-api` carries its own HTTP parser/serializer, header/body limits, keep-alive handling, and 64 KiB response buffer | These are transport responsibilities that could move behind a proven lighttpd application protocol if the total resource cost improves. |
| Approximately 80 routes still perform dynamic printer/Cura work | The application service remains necessary even if all generic HTTP transport moves to lighttpd. |

The immediate priority is fixing the observed four-SSE-client starvation,
retained descriptors, and duplicate-API-process behavior across the existing
proxy/API boundary. The frontend should remain framework-free; product work
should focus on lifecycle accuracy, storage/upload UX, security, diagnostics,
updates/rollback, accessibility, and hardware-backed browser workflows. See
[PROJECT_STATUS.md](PROJECT_STATUS.md) and
[PLATFORM_MODERNIZATION_ROADMAP.md](PLATFORM_MODERNIZATION_ROADMAP.md).

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
- `GET /api/v1/printer` — full printer state, including Deneb-native
  `native_active` and `native_stop_allowed` flags from `deneb-printsvc`
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
backend name, status and command endpoint URLs, `native_printsvc`, and
`native_only_route` for lab validation.

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
protected by Open Access or Deneb auth. Cura 5.13 discovery, upload, conflict,
print, monitor, pause/resume, cancel, and restart-recovery slices have target
proof; broader versions and failure cleanup remain open.

## Current Limits

| Area | Current state | Required before first-class status |
| --- | --- | --- |
| Connection handling | Three SSE clients plus polling passed; four SSE clients starved REST and retained seven descriptors until reboot | Isolate SSE capacity, close abandoned proxy/API sockets, and pass reconnect/slow-client/long-duration tests |
| Print controls | Status/progress has target proof; Web pause/resume/cancel and stale-state recovery are not fully proven | Hands-on current-package lifecycle, reconnect, concurrent-client, and failure recovery tests |
| Upload and storage | UM API and Cura multipart upload/start exist | Upload progress/cancel, free-space checks, failed-upload cleanup, history, local/USB file management, and removal-safe behavior |
| Security | Web/API supports Open Access or Deneb auth; stock Cura cluster writes are intentionally unauthenticated for compatibility | Deliberate trusted-LAN policy, session/logout UX, expiry, rate limits, origin/CSRF policy, and request audit |
| Diagnostics and updates | Basic log retrieval exists | Redacted diagnostics export plus update, reboot, rollback, and degraded-state UX |
| Identity | Some firmware, machine, and PCB fields are missing or invalid on the current target | Populate identity from authoritative device/config sources and test Cura/Web presentation |
| Motion | Guarded X/Y/Z controls exist | Keep home/limit/volume interlocks; do not expose extruder jogging until safe-temperature and material-context gates are proven |
| Accessibility | Functional MVP only | Responsive layout, keyboard use, readable errors, accessibility review, and browser compatibility matrix |

Shared state, pending-job, formatting, macro, and command ownership is tracked in
[PRINTSVC_INTEGRATION_AUDIT.md](PRINTSVC_INTEGRATION_AUDIT.md). Do not repeat
implementation-by-implementation migration logs here; this file should describe
the current external behavior and remaining product gaps.

## Deneb Extensions

- `GET /api/v1/deneb/events` — SSE stream (1 Hz status updates)
- `GET /api/v1/deneb/version` — Deneb version info
- `POST /api/v1/deneb/setup` — First-connect setup
- `POST /api/v1/deneb/auth` — Session authentication

## Configuration

The web UI is installed, enabled, and restarted by the Deneb update package.
Cura/mDNS advertisement remains configurable:
```sh
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
