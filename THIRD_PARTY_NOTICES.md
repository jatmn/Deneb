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
- lighttpd 1.4.76 is distributed as the HTTP front end under its BSD license. The upstream COPYING notice is packaged as LIGHTTPD_LICENSE.txt.

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
- `LodePNG` 20230410, copyright 2005-2023 Lode Vandevenne, zlib-style
  license, notice retained in `ui/lib/lvgl/src/libs/lodepng/lodepng.c` and
  `lodepng.h`

These notices are retained in the LVGL submodule and included in release
packages.

## Package Inventory Reconciliation

Audited 2026-07-22. Current `.deneb` packages redistribute Deneb's MPL-2.0
components, LVGL and its selected bundled helpers (including LodePNG), libzmq
4.3.5, mbedTLS 2.28.8, and lighttpd 1.4.76. The Cura plugin is distributed
separately under LGPL-3.0-or-later; the material identifier dataset is retained
in source form under CC0-1.0. The stock legal screen was used only as a
comparison point: this notice inventory is derived from the actual Deneb
payload, not copied from stock notices.

## Ongoing Release Checks

- Re-run the dependency and provenance inventory when a dependency or generated dataset changes.
- Preserve notices for any open-source components we redistribute.
- Include the project license, this notice file, and any required third-party
  license files in release artifacts that redistribute binary or source forms.
- Reconcile this file against each actual release payload before publishing it.
- Update touchscreen and web UI about/legal screens when dependencies are added, removed, or replaced.
