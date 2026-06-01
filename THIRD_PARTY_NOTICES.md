# Third-Party Notices

This file tracks third-party components and notice obligations for original
Deneb releases. This is not legal advice.

## Runtime Components

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

## Required Before Release

- Inventory dependencies used by the Deneb installer, touchscreen UI, web UI, LAN API shim, update checker, and diagnostics tooling.
- Preserve notices for any open-source components we redistribute.
- Include the project license, this notice file, and any required third-party
  license files in release artifacts that redistribute binary or source forms.
- Compare this file against the stock printer touchscreen/about/legal disclosure before publishing release artifacts.
- Update touchscreen and web UI about/legal screens when dependencies are added, removed, or replaced.
