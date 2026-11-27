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
| lighttpd | lighttpd upstream | 1.4.85 | BSD-3-Clause | HTTP front end built from pinned, hashed source; not vendored |
| musl libc | OpenWrt MT76x8 toolchain, see [MIPS cross-toolchain](#mips-cross-toolchain) | 1.2.5 with OpenWrt backports | MIT and bundled notices | Statically linked; upstream copyright and license packaged |
| GCC runtime libraries | OpenWrt MT76x8 toolchain, see [MIPS cross-toolchain](#mips-cross-toolchain) | 14.3.0 | GPL-3.0-or-later WITH GCC-exception-3.1 | `libgcc` and `libstdc++` code statically linked where used; license and exception packaged |
| Pillow bootstrap host tool | `https://github.com/python-pillow/Pillow` | 13.0.0 / `293218ad059d994baf1777d882911bed6a49704a` | MIT-CMU | Host-only splash converter dependency; wheels are version/hash locked in `tools/bootstrap-requirements.txt`; source archive `pillow-13.0.0.tar.gz` SHA-256 is `cdcd1ba258178f7356d74170c9a81ba4eab6a5d55748bc402a5a2a6fe6be3510` |
| ESLint host checker | registry.npmjs.org `eslint` 10.15.0, `@eslint/js` 10.0.1, and `globals` 17.12.0 | Locked by `tools/eslint/package-lock.json`. `eslint-10.15.0.tgz` SHA-256 is `8cb5e45fb5b1174e4216d546b69d37c0ac5cb5d9ba8342946a04af1f59605edb` | MIT | Host CI checker for existing browser JavaScript. Not installed on the printer and not copied from `web/www` into firmware packages |
| API, IPC, and Cura compatibility | Public printer APIs, public repositories/documentation, and interoperability observations | Current Deneb implementation | MPL-2.0 | Clean-room Deneb code; no stock application source copied |
| Print-service macros and G-code policies | Public G-code/controller documentation and observed printer behavior | Current Deneb implementation | MPL-2.0 | Minimal Deneb-authored sequences; no stock macro file copied |
| Hugo | `gohugoio/hugo` | 0.170.1 (pinned in `website/vendor-pins/hugo.version`; `hugo_0.170.1_linux-amd64.tar.gz` SHA-256 `841a5462853aefc9942e4de1e8a5d75584b6a5181941e6ad3f1d745190e03cd8`) | Apache-2.0 | Public-site generator only; not a printer runtime |
| Hextra | `github.com/imfing/hextra` | v0.13.0, locked by `website/go.sum` | MIT | Docs theme for `website/` |
| FlexSearch | `nextapps-de/flexsearch` via the npm registry | 0.8.143 in `website/package-lock.json`, bundle SHA-256 `433e941a8a573ebb9931fc16fc75266ab6b93f569ac2fb4f3dc66882e0416f4c` | Apache-2.0 | Only the verified browser bundle is copied into the generated static site |
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

## MIPS cross-toolchain

The native package uses the [OpenWrt 25.12.6 ramips/mt76x8 toolchain](https://downloads.openwrt.org/releases/25.12.6/targets/ramips/mt76x8/).
Its package archive has SHA-256 `628559363d3e41da6cc298ed0a27b81fbc3843a2758228c7697861defd324da3`.
The compiler identifies OpenWrt build source revision
[`981c978eff636fc7d81f0ed17b00e2a6317c949a`](https://github.com/openwrt/openwrt/tree/981c978eff636fc7d81f0ed17b00e2a6317c949a/toolchain),
which records the build recipes and patches.

| Runtime upstream source | Version | Source archive SHA-256 |
| --- | --- | --- |
| [musl](https://musl.libc.org/releases/musl-1.2.5.tar.gz) | 1.2.5 | `a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4` |
| [GCC](https://ftp.gnu.org/gnu/gcc/gcc-14.3.0/gcc-14.3.0.tar.xz) | 14.3.0 | `e0dc77297625631ac8e50fa92fffefe899a4eb702592da5c32ef04e2293aca3a` |

OpenWrt's [musl patch set](https://github.com/openwrt/openwrt/tree/981c978eff636fc7d81f0ed17b00e2a6317c949a/toolchain/musl/patches)
backports the upstream `iconv` and `qsort` fixes for CVE-2026-6042 and
CVE-2026-40200. The standard OpenWrt build omits the EUC-KR converter
affected by CVE-2025-26519 through its `900-iconv_size_hack.patch`.
OpenWrt supplies `libssp_nonshared.a` in the musl sysroot and
`libgcc_eh.a` from GCC. GCC patches and target settings are pinned by the
same OpenWrt source revision.

When a dependency version or generated dataset changes:

1. Record its upstream URL, immutable revision, license, and archive SHA-256.
2. Reproduce generated content from that exact source and review any local-only
   records rather than silently carrying them forward.
3. Update `REUSE.toml`, `LICENSES/`, this inventory, and release notices.
4. Run `reuse lint`, the publication-boundary check, and the full-history secret
   scan before publication or release.
