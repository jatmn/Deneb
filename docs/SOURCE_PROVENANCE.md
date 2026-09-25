# Source Provenance

Last audited: 2026-09-25

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
| Pillow bootstrap host tool | `https://github.com/python-pillow/Pillow` | 12.3.0 / `bb1d8e8ab8d29048624d96e3ee53cecf7c13d13d` | MIT-CMU | Host-only splash converter dependency; wheels are version/hash locked in `tools/bootstrap-requirements.txt`; source archive `pillow-12.3.0.tar.gz` SHA-256 is `3b8182a766685eaa002637e28b4ec8d6b18819a0c71f579bf0dbaa5830297cce` |
| ESLint host checker | registry.npmjs.org `eslint` 10.11.0, `@eslint/js` 10.0.1, and `globals` 17.12.0 | Locked by `tools/eslint/package-lock.json`. `eslint-10.11.0.tgz` SHA-256 is `2c1c9adad9ba0d7c8b885216a920a46eb4c1d400d04f8baafe2b281aaea945f0` | MIT | Host CI checker for existing browser JavaScript. Not installed on the printer and not copied from `web/www` into firmware packages |
| API, IPC, and Cura compatibility | Public printer APIs, public repositories/documentation, and interoperability observations | Current Deneb implementation | MPL-2.0 | Clean-room Deneb code; no stock application source copied |
| Print-service macros and G-code policies | Public G-code/controller documentation and observed printer behavior | Current Deneb implementation | MPL-2.0 | Minimal Deneb-authored sequences; no stock macro file copied |
| Hugo extended | `gohugoio/hugo` | 0.166.0 (pinned in `website/vendor-pins/hugo.version`; tarball SHA-256 `0e39b901e3f919f1daae05c8ff64f0c14c8a348ef46886d63f8e6d1bb2653885`) | Apache-2.0 | Public-site generator only; not a printer runtime |
| Hextra | `github.com/imfing/hextra` | Hugo module, locked by `website/go.sum` | MIT | Docs theme for `website/` |
| FlexSearch | `nextapps-de/flexsearch` | 0.8.143, SHA-256 `433e941a8a573ebb9931fc16fc75266ab6b93f569ac2fb4f3dc66882e0416f4c` | Apache-2.0 | Downloaded at site-build time; not committed |
| Space Grotesk | Bunny Fonts / OFL project | Runtime CSS from `fonts.bunny.net` | OFL-1.1 | Public-site webfont |
| IBM Plex Sans and IBM Plex Mono | Bunny Fonts / OFL project | Runtime CSS from `fonts.bunny.net` | OFL-1.1 | Public-site webfonts |

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
