# Source Provenance

Last audited: 2026-07-22

This document records the source and license boundary for material that is not
original Deneb work. It is an engineering inventory, not legal advice.

| Path or component | Source | Revision/version | License | Treatment |
| --- | --- | --- | --- | --- |
| Original Deneb files | This repository | Current tree | MPL-2.0 | Default declared by `REUSE.toml` |
| Cura integration plugin | Deneb adapter for UltiMaker Cura | Tested with Cura 5.13 | LGPL-3.0-or-later | Separate override; no Cura source is copied |
| `web/src/api_cluster_materials.h` | `Ultimaker/fdm_materials` | `886e7ad927463493cc9c64f427b1ae2cf4ce12c1` | CC0-1.0 | 280 exact GUID/version records; one malformed upstream GUID omitted |
| LVGL | `lvgl/lvgl` submodule | Pinned by Git | MIT plus bundled helper notices | License files retained and packaged |
| ZeroMQ/libzmq | `zeromq/libzmq` | 4.3.5 | MPL-2.0 | Statically linked; source URL and notice packaged |
| mbedTLS | Mbed TLS upstream | 2.28.8 | Apache-2.0 OR GPL-2.0-or-later | Build dependency for `deneb-dfsvc`; not vendored |
| lighttpd | lighttpd upstream | 1.4.76 | BSD-3-Clause | HTTP front end built from pinned, hashed source; not vendored |
| API, IPC, and Cura compatibility | Public printer APIs, public repositories/documentation, and interoperability observations | Current Deneb implementation | MPL-2.0 | Clean-room Deneb code; no stock application source copied |
| Print-service macros and G-code policies | Public G-code/controller documentation and observed printer behavior | Current Deneb implementation | MPL-2.0 | Minimal Deneb-authored sequences; no stock macro file copied |

The previously tracked `docs/ultimaker-api-v1.json` was a 77,019-byte Swagger
description obtained from the unauthenticated REST documentation surface of a
publicly accessible UltiMaker printer from another model family. It was used as
an interoperability reference, not as application source, and was removed when
it was no longer needed. The historical Git object is intentionally retained.
This origin record does not claim that public accessibility alone grants a
redistribution license.

Full vendor firmware images, extracted root filesystems, proprietary binaries,
device identifiers, and credentials remain outside the repository boundary.
Deneb's API, IPC, touchscreen, services, compatibility code, and macro sequences
are clean-room implementations based on publicly available resources and
observed interoperability behavior.

When a dependency version or generated dataset changes:

1. Record its upstream URL, immutable revision, license, and archive SHA-256.
2. Reproduce generated content from that exact source and review any local-only
   records rather than silently carrying them forward.
3. Update `REUSE.toml`, `LICENSES/`, this inventory, and release notices.
4. Run `reuse lint`, the publication-boundary check, and the full-history secret
   scan before publication or release.
