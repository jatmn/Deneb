# Deneb Cura Integration

Deneb now has a Cura local-network compatibility layer for the UltiMaker 2+
Connect. It is implemented as a small Deneb web/API runtime on the printer plus
a Cura plugin that teaches Cura how to map Deneb's advertised network machine
id back to Cura's stock UM2+ Connect profile.

## Components

- `deneb-mdns` advertises `_ultimaker._tcp.local.` with Cura-compatible TXT
  fields.
- `deneb-api` serves the UltiMaker REST API v1-shaped endpoints and the
  single-printer `/cluster-api/v1/` endpoints Cura polls.
- `cura/plugins/DenebUM2CNetworkPrinting` is the Cura plugin package source.
- `tools/build-cura-plugin.ps1` builds `dist/DenebUM2CNetworkPrinting.curapackage`.

## Discovery Model

Deneb advertises the printer with `type=printer`, `machine=deneb_um2c`, a
`name` formatted as `<configured printer name> (Deneb UM2C)`, and a
Cura-compatible `firmware_version`. Cura rejects local-network devices whose
advertised firmware version is older than `4.0.0`, so Deneb defaults the mDNS
firmware TXT value to `4.0.0`.

The real Deneb build version remains available from:

- `GET /api/v1/deneb/version`

Cura maps local-network machines by advertised machine/BOM metadata. The stock
UM2+ Connect Cura definition is network-capable but does not currently expose a
BOM value that Deneb can advertise directly. The Deneb plugin maps
`deneb_um2c` to Cura's stock `ultimaker2_plus_connect` machine so existing UM2+
Connect materials, variants, quality profiles, and printer presets are reused.

Do not patch Cura's bundled resources or copy definition files into Cura's user
profile directly.

## Printer API Surface

Deneb implements the core cluster endpoints Cura 5.13 polls for monitor,
upload, and basic print-job actions:

- `GET /cluster-api/v1/materials`
- `POST /cluster-api/v1/materials`
- `GET /cluster-api/v1/printers`
- `GET /cluster-api/v1/print_jobs`
- `POST /cluster-api/v1/print_jobs`
- `POST /cluster-api/v1/print_jobs/`
- `PUT /cluster-api/v1/print_jobs/{uuid}/action`
- `DELETE /cluster-api/v1/print_jobs/{uuid}`
- `GET /cluster-api/v1/print_jobs/{uuid}/preview`

The UM API v1-shaped web endpoints also include:

- `POST /api/v1/print_job`
- `PUT /api/v1/print_job/state`
- `GET /api/v1/print_job`
- `GET /api/v1/materials`

Cluster upload/control is intentionally unauthenticated for stock Cura
compatibility because current Cura does not send Deneb web-session credentials
on these cluster write requests. The Deneb web UI and UM API v1 write endpoints
remain protected by Open Access or Deneb auth.

## Upload And Start Flow

For Cura cluster uploads, Deneb streams the multipart upload through `deneb-api`
instead of buffering whole jobs in RAM. A pending-job metadata file at
`/tmp/deneb-cluster-print-job.json` keeps the job visible while Deneb validates
metadata, waits for conflict confirmation, prepares, and preheats.

Current Cura sends UM2+ Connect jobs as `.ufp` archives. Deneb extracts
`3D/model.gcode` before native registration so the material/nozzle header is
validated before any motion starts. A 2026-06-14 Cura 5.13 local-network test on
package `ff49e86b` proved this path for material-mismatch prompt, Cancel,
Continue/start, completion, pause/resume, cancel/abort back to idle, and pending
mismatch recovery after UI/API/print-service restarts. The first pre-fix
Cura-local `.ufp` upload is retained as negative evidence: the raw archive
reached native print registration and produced a Marlin payload ASCII error.

Upload registration, conflict continue/cancel, and pending-job cancel now use
native Deneb code paths. `deneb-api` assigns a native pending-job tracker,
writes Cura-visible pending metadata from UCI/file hints, leaves material/nozzle
conflicts waiting for user confirmation, and starts no-conflict jobs with a
native `JOB` command. The native `deneb-printsvc` milestone still owns the final
driver-side replacement and live validation of this flow.

## Cura Plugin Build

Build the plugin package from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-cura-plugin.ps1
```

Install `dist/DenebUM2CNetworkPrinting.curapackage` through Cura's package
install flow, then restart Cura before testing discovery.

## Remaining Release Blockers

- Validate status/progress updates and broader failure modes against current
  Cura builds on real hardware. Discovery, upload, material-mismatch prompt,
  Cancel, Continue/start, completion, pause/resume, cancel/abort back to idle,
  and pending mismatch recovery after UI/API/print-service restarts are covered
  by the 2026-06-14 Cura 5.13 local-network run on package `ff49e86b`.
- Review stock firmware progress/time calculation and implement parity. The
  Cura-local completion stayed at `progress:0.0`, `time_total:0`, and
  `time_elapsed:0`.
- Confirm local-storage and USB-removal-safe behavior for uploaded jobs.
- Add free-space and failure-mode validation around uploads.
- Decide how much of the current web/touch/API print-control duplication moves
  into the native `deneb-printsvc` rewrite.
- Keep cluster API compatibility tests in sync with Cura behavior rather than
  only with static endpoint assumptions.
