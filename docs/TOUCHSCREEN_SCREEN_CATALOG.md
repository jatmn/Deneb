# Touchscreen Screen Catalog

Reference catalog for the current Deneb touchscreen UI. These screenshots are
host-rendered from the LVGL UI at the target 320x240 resolution with
`BACKEND_COMM_STUB=ON`. They are deterministic layout references, not captures
from a particular printer: temperatures, network values, USB files, Digital
Factory status, and error details use stub data.

Screenshot set regenerated from commit `ab2741a` on 2026-07-22. The host
renderer proves the current screen layouts and default states. It cannot prove
hardware- or cloud-backed workflows; those remain explicitly listed below for
target capture.

Regenerate the complete host catalog from PowerShell at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools/capture-ui-screens.ps1 `
  -OutputDirectory docs/touchscreen-screens
```

The wrapper builds the host UI with stub drivers, writes temporary PPM files,
converts them to PNG with Python 3, and removes the temporary PPM directory.
Python 3 is installed by `tools/setup-wsl-build.sh`.

Regenerate one screen by slug after a host build:

```powershell
powershell -ExecutionPolicy Bypass -File tools/capture-ui-screens.ps1 `
  -NoBuild -Screen status -OutputDirectory docs/touchscreen-screens
```
## Top-Level Flow

Home is the root menu. Most screens show a 32 px title bar and a back button
when entered from the navigation stack. The UI is optimized for short actions
on a 320x240 resistive touchscreen. Unlike the tap-only stock firmware, Deneb
supports vertical swipe scrolling for menus, file lists, and long content, plus
horizontal drag sliders on Temperature, active material workflows, and Frame
Lighting. Dragging manipulates the visible content or control; it does not
provide global left/right screen navigation. Use the visible buttons and Back
control to change screens.

![Home](touchscreen-screens/home.png)

## Screen Catalog

| Screen | Screenshot | Current Role | Primary Actions | Capture Scope |
|---|---|---|---|---|
| Home | <img src="touchscreen-screens/home.png" width="160" alt="Home screen"> | Scrollable root menu for all operational areas. | Open Status, Print from USB, Material, Maintenance, Manual Control, Temperature, and Settings. | Layout and navigation only; the final menu item may be below the viewport. |
| Status | <img src="touchscreen-screens/status.png" width="160" alt="Status screen"> | Live print overview. | Show state, nozzle/bed readings, progress, remaining time, active file, XY/Z position, Pause/Resume, and Stop. | Host capture uses idle/default telemetry; capture active, pausing, resuming, and stopping states on target. |
| Print from USB | <img src="touchscreen-screens/print-from-usb.png" width="160" alt="Print from USB screen"> | USB-file selection and print control surface. | Refresh files; select a G-code file; start, open Material, pause, resume, or stop a job. | Host capture proves empty/default layout, not a populated USB or an active job. |
| Print Conflict | <img src="touchscreen-screens/print-conflict.png" width="160" alt="Print conflict screen"> | Confirmation view for a pending job that needs operator choice. | Continue the pending job or cancel it. | Capture real material-mismatch and preheat/continue paths on target. |
| Material | <img src="touchscreen-screens/material.png" width="160" alt="Material screen"> | Material workflow hub. | Load, unload, choose a material, move filament, finish movement, import profiles, and set workflow temperature. | Host state is idle; busy, movement, and error gating require target captures. |
| Set Material | <img src="touchscreen-screens/set-material.png" width="160" alt="Set Material screen"> | Material catalog selection. | Select the installed material profile. | Capture any imported/custom profiles on target if they become supported. |
| Maintenance | <img src="touchscreen-screens/maintenance.png" width="160" alt="Maintenance screen"> | Hardware-maintenance navigation. | Open Temperature, Update Firmware, Move Build Plate, Level Build Plate, and Diagnostics. | Layout reference. |
| Temperature | <img src="touchscreen-screens/temperature.png" width="160" alt="Temperature screen"> | Nozzle and bed target controls. | Drag nozzle/bed targets, apply them, or cool down both heaters. | Host values are placeholders; heating/cooldown outcomes require target evidence. |
| Update Firmware | <img src="touchscreen-screens/update-firmware.png" width="160" alt="Update Firmware screen"> | Deneb package update chooser. | List USB `.deneb` packages, select a package, check pending releases, and confirm installation with a second tap. | Capture USB-populated and confirmation/progress states on target. |
| Manual Control | <img src="touchscreen-screens/manual-control.png" width="160" alt="Manual Control screen"> | Motion and build-plate control. | Jog X/Y, home XY, choose step size, home Z, and move the build plate up or down. | Position values and hardware responses require target capture. |
| Level Build Plate | <img src="touchscreen-screens/level-build-plate.png" width="160" alt="Level Build Plate screen"> | Guided leveling sequence. | Advance the current leveling step with the single contextual action button. | Capture each target-confirmed stage if documenting the complete procedure. |
| Diagnostics | <img src="touchscreen-screens/diagnostics.png" width="160" alt="Diagnostics screen"> | Hardware summary and log-export surface. | Display Air Manager/build-volume/fan information and export diagnostics to USB. | Stub reports unknown/default data; capture a real device and export result. |
| Settings | <img src="touchscreen-screens/settings.png" width="160" alt="Settings screen"> | Scrollable system-settings navigation. | Open Language, Nozzle Size, Network, Digital Factory, Frame Lighting, Factory Reset, and About Deneb. | Layout reference. |
| Language | <img src="touchscreen-screens/language.png" width="160" alt="Language screen"> | Runtime locale selector. | Choose English, Dutch, German, French, Simplified Chinese, Pirate English, or L33T English. | Refresh fonts and this capture when locale strings or glyph coverage change. |
| Nozzle Size | <img src="touchscreen-screens/nozzle-size.png" width="160" alt="Nozzle Size screen"> | Installed-nozzle configuration. | Select the configured nozzle diameter. | Capture selected-state persistence on target when needed. |
| Network | <img src="touchscreen-screens/network.png" width="160" alt="Network screen"> | Network status and USB configuration. | Show hostname/WiFi/Ethernet status; toggle WiFi; import WiFi or Ethernet settings; reset Ethernet to DHCP. | Host capture uses deterministic `Deneb-Printer` and placeholder status values. |
| Digital Factory | <img src="touchscreen-screens/digital-factory.png" width="160" alt="Digital Factory screen"> | Native Digital Factory connection control. | Pair/show PIN, display bridge status, and enable Disconnect only in a disconnectable state. | Host capture is the default unpaired state; pairing, connected, reconnecting, disconnecting, service-error, material-mismatch, cloud-print, and print-job-action states require target/cloud capture. |
| Frame Lighting | <img src="touchscreen-screens/frame-lighting.png" width="160" alt="Frame Lighting screen"> | Frame-light control. | Turn the frame light on or off and drag brightness. | Verify actual brightness hardware response on target. |
| Factory Reset | <img src="touchscreen-screens/factory-reset.png" width="160" alt="Factory Reset screen"> | Local-settings reset confirmation. | Tap Reset, then tap again to reset local settings and reboot. | Keep this host-only reference; do not invoke solely for documentation. |
| About Deneb | <img src="touchscreen-screens/about.png" width="160" alt="About Deneb screen"> | Version and device identity reference. | Show Deneb version, stock base, repository, printer ID, and certifications. | Host version reflects the local build and may carry `-dirty`; capture a released target build for support docs. |
| Error | <img src="touchscreen-screens/error.png" width="160" alt="Error screen"> | Blocking recovery prompt. | Show an ER code, description, recommended action, and dismiss with OK. | Host capture uses synthetic `ER999`; add real ER-code examples only with their verified recovery guidance. |
## Coverage Gaps To Capture Later

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
