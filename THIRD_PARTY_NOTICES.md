# Third-Party Notices

This file tracks third-party components and notice obligations for original
Deneb releases. This is not legal advice.

## Runtime Components

### Cura plugin and material identifiers

- The optional Cura plugin is LGPL-3.0-or-later and integrates with Cura without copying Cura source.
- web/src/api_cluster_materials.h contains 280 GUID/version records derived from Ultimaker/fdm_materials commit 886e7ad927463493cc9c64f427b1ae2cf4ce12c1 under CC0-1.0. One malformed upstream GUID is omitted.

See docs/SOURCE_PROVENANCE.md for the audited source boundary.

### mbedTLS and lighttpd

- mbedTLS 2.28.8 is statically linked into deneb-dfsvc under its Apache-2.0 OR GPL-2.0-or-later choice. The upstream LICENSE is packaged as MBEDTLS_LICENSE.txt.
- lighttpd 1.4.85 is distributed as the HTTP front end under its BSD license. The upstream COPYING notice is packaged as LIGHTTPD_LICENSE.txt.

### Pillow bootstrap host tool

- Pillow 13.0.0 is used only on the build host to convert the Deneb splash to
  RGB565. It is installed from repository-locked wheel hashes and is not
  included in target packages.
- Upstream: https://github.com/python-pillow/Pillow at commit
  `293218ad059d994baf1777d882911bed6a49704a`
- License: MIT-CMU, retained in `notices/PILLOW-13.0.0-LICENSE.txt`

### ESLint host checker

- ESLint 10.15.0, with `@eslint/js` 10.0.1 and `globals` 17.12.0, checks the
  existing browser JavaScript during CI. Versions are locked in
  `tools/eslint/package-lock.json`.
- Upstream registry: https://registry.npmjs.org/eslint/-/eslint-10.15.0.tgz
- License: MIT. The ESLint and `@eslint/js` notice is
  `notices/ESLINT-10.15.0-LICENSE.txt`. The `globals` notice is
  `notices/GLOBALS-17.12.0-LICENSE.txt`.
- This checker runs on the build host only. It is not part of a `.deneb`
  package, the get-started image, or the printer about/legal screen.

### LVGL

- Component: LVGL runtime subset
- Location: `ui/lib/lvgl`
- Upstream: https://github.com/lvgl/lvgl
- License: MIT
- Repository form: pinned git submodule
- Notice files kept in the LVGL submodule:
  - `ui/lib/lvgl/LICENCE.txt`
  - `ui/lib/lvgl/COPYRIGHTS.md`

Deneb builds against the pinned LVGL submodule for the native touchscreen UI.
Release packages include LVGL's MIT license notice.

### ZeroMQ / libzmq

- Component: ZeroMQ C library, statically linked into `deneb-ui`
- Upstream: https://github.com/zeromq/libzmq
- License: MPL-2.0
- Notice/source file included in release packages:
  - `notices/libzmq-4.3.5-NOTICE.txt`
  - `notices/MPL-2.0.txt`

Deneb links against a locally built static `libzmq.a` for backend IPC and the
Digital Factory bridge. Source is not vendored in this repository; builds
currently use the cached `zeromq-4.3.5` source noted in the release build
script. The release package notice tells recipients where to obtain the
corresponding libzmq source.

### LVGL bundled helpers used by the selected runtime subset

LVGL's selected runtime subset includes small bundled helper implementations
under `ui/lib/lvgl/src/stdlib/builtin`:

- `printf`, MIT license, notice in
  `ui/lib/lvgl/src/stdlib/builtin/LICENSE_SPRINTF.txt`
- `tlsf`, BSD-style license, notice in
  `ui/lib/lvgl/src/stdlib/builtin/LICENSE_TLSF.txt`

These notices are retained in the LVGL submodule and included in release
packages.

### LodePNG

- Version `20261102`, [upstream source at 7b2893e](https://github.com/lvandeve/lodepng/tree/7b2893e35891c0c85bee5595caafaf3c2044b409)
- Copyright (c) 2005-2026 Lode Vandevenne
- Zlib license retained in `common/lodepng/lodepng.c` and `lodepng.h` and
  included in release packages as `LODEPNG_LICENSE.txt`

Deneb uses this snapshot in all four native executables that consume LodePNG.
The touchscreen build retains LVGL's allocation, filesystem, and draw-buffer
integration. The print service, web API, and Digital Factory service build the
same source with only the inflate/zlib decoder enabled.

## Package Inventory Reconciliation

Audited 2026-07-22. Current `.deneb` packages redistribute Deneb's MPL-2.0
components, LVGL and its selected bundled helpers, LodePNG 20261102, libzmq
4.3.5, mbedTLS 2.28.8, and lighttpd 1.4.85. The Cura plugin is distributed
separately under LGPL-3.0-or-later; the material identifier dataset is retained
in source form under CC0-1.0. The stock legal screen was used only as a
comparison point: this notice inventory is derived from the actual Deneb
payload, not copied from stock notices.

## Public website (deneb3d.dev)

These components are used to generate or render the public docs site. They are
not installed on the printer.

- Hugo extended 0.166.0, Apache-2.0, https://github.com/gohugoio/hugo
- Hextra, MIT, https://github.com/imfing/hextra (Hugo module)
- FlexSearch 0.8.143, Apache-2.0, https://github.com/nextapps-de/flexsearch
  (fetched at build; pin in `website/vendor-pins/flexsearch.bundle.min.js.sha256`)
- Space Grotesk, OFL-1.1, loaded from https://fonts.bunny.net
- IBM Plex Sans and IBM Plex Mono, OFL-1.1, loaded from https://fonts.bunny.net
- bunny.net favicon, trademark of bunny.net, loaded at runtime from
  https://bunny.net/favicon.ico for the affiliate footer mark

## Ongoing Release Checks

- Re-run the dependency and provenance inventory when a dependency or generated dataset changes.
- Preserve notices for any open-source components we redistribute.
- Include the project license, this notice file, and any required third-party
  license files in release artifacts that redistribute binary or source forms.
- Reconcile this file against each actual release payload before publishing it.
- Update touchscreen and web UI about/legal screens when dependencies are added, removed, or replaced.
