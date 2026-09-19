# Touchscreen Screen Catalog (host capture)

Operator screen descriptions live at https://deneb3d.dev/docs/touchscreen/

This file is the host-renderer capture procedure. Screenshots are written to
`docs/touchscreen-screens/` from the LVGL UI at 320x240 with
`BACKEND_COMM_STUB=ON`. They are deterministic layout references with stub
data, not live printer photos.

Screenshot set regenerated from commit `ab2741a` on 2026-07-22.

## Regenerate

Complete catalog from PowerShell at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools/capture-ui-screens.ps1 `
  -OutputDirectory docs/touchscreen-screens
```

The wrapper builds the host UI with stub drivers, writes temporary PPM files,
converts them to PNG with Python 3, and removes the temporary PPM directory.
Python 3 is installed by `tools/setup-wsl-build.sh`.

One screen by slug after a host build:

```powershell
powershell -ExecutionPolicy Bypass -File tools/capture-ui-screens.ps1 `
  -NoBuild -Screen status -OutputDirectory docs/touchscreen-screens
```

## Coverage gaps to capture later

The host catalog is current for static layout and default state. Add target or
cloud-backed captures for these stateful workflows:

- Active print with non-zero progress, remaining time, pause/resume, and stop.
- USB file browser with real files, selection preview, material mismatch,
  preheat/continue, active-print, and abort states.
- Material load/unload/move workflows while busy, moving, or faulted.
- Network with WiFi connected/disabled and static Ethernet applied.
- Digital Factory pairing PIN, paired, reconnecting, disconnecting,
  service-error, material-mismatch/cloud-print, and print-job-action states.
- Firmware-package listing, confirmation, and target-side installation status.
- Real diagnostics/log-export feedback and ER-code recovery examples.
