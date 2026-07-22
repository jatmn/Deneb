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

The previously tracked `docs/ultimaker-api-v1.json` was unused and had no
documented origin or redistribution basis, so it was removed instead of being
presented as publishable project material. Full vendor firmware images,
extracted root filesystems, proprietary binaries, device identifiers, and
credentials remain outside the repository boundary.

History audit note: the removed API JSON still exists in Git history. The repository remains private. Before any future public visibility change, either establish that historical file's redistribution basis or rewrite it from all refs, then rerun the full-history secret and artifact scans.

When a dependency version or generated dataset changes:

1. Record its upstream URL, immutable revision, license, and archive SHA-256.
2. Reproduce generated content from that exact source and review any local-only
   records rather than silently carrying them forward.
3. Update `REUSE.toml`, `LICENSES/`, this inventory, and release notices.
4. Run `reuse lint`, the publication-boundary check, and the full-history secret
   scan before publication or release.
