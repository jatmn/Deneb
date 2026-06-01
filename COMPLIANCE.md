# Compliance Notes

This is not legal advice. It is the working project boundary for Deneb.

## Repository Boundary

- Do not commit full UltiMaker firmware images.
- Do not commit extracted full root filesystems.
- Do not commit `/home/cygnus`, `/home/lib/stardustWebsocketProtocol`, stock web assets, binaries, or modified complete firmware images.
- Do not commit recovered or decompiled vendor source unless a license clearly permits it.
- Do not commit private keys, signing secrets, device credentials, access tokens, or device-unique identifiers.
- Do commit original Deneb code, documentation, build scripts, installer scripts, compatibility implementations, and minimal patch/transformation logic where needed.

## License Plan

- Original Deneb installer, web UI, LAN API shim, touchscreen replacement, and service code: `MPL-2.0`.
- LVGL runtime dependency: MIT; keep `ui/lib/lvgl` as a pinned submodule,
  preserve `LICENCE.txt` and `COPYRIGHTS.md`, and include the LVGL notice in
  release packages.
- Statically linked libzmq: MPL-2.0; keep release notices current with the
  exact libzmq source used by the release build, and tell recipients where to
  obtain that source.
- Cura-derived compatibility code, if any: use Cura-compatible terms and preserve attribution.
- Marlin firmware work: keep in a clearly separate GPL-compatible tree or fork and satisfy Marlin obligations.
- Documentation license is still to be decided unless covered by the project default.

## Publication Approach

Deneb should be an addon/mod kit built around user-supplied firmware or a
user-owned printer. Release artifacts should contain only Deneb-owned files,
metadata, manifests, signatures, scripts needed to install or remove Deneb
changes, and license/notice files for third-party components that Deneb
redistributes or links into shipped binaries.

## Official Firmware Escape Hatch

Deneb should preserve a documented way for a user to intentionally install official UltiMaker firmware. Deneb signatures must never be presented as UltiMaker signatures.
