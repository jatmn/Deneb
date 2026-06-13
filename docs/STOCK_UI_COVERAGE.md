# Stock UI Coverage

This compares the stock Cygnus touchscreen menus on an UltiMaker 2+ Connect
against the current native Deneb LVGL UI. The stock reference was checked
against the extracted recovery image at `rootfs/home/cygnus/menu`.

## Stock Top Level

Stock `MainMenuPage` exposes:

- Select from USB
- Materials
- Maintenance
- Settings
- Printer name/status header
- Digital Factory connection indicator
- Firmware update notification path, which Deneb intentionally does not use

Deneb now exposes:

- Status
- Print from USB
- Material
- Maintenance
- Manual Control
- Temperature
- Settings

## Coverage Matrix

| Stock area | Stock options | Deneb status |
| --- | --- | --- |
| Print | Select from USB, browse, preview, prepare, pause/resume/abort, conflict/error pages | Partial. USB browse, file selection, explicit start controls, pause/resume/abort controls, preheat Stop availability, mismatch continue handling, and abort status cleanup exist. Thumbnail preview and richer stock conflict/error pages are still incomplete. |
| Materials | Change material, load material, unload material, set material, move material; import material appears inside Set Material as "Add a new material" | Mostly present. Load/unload/change/move shortcuts exist; Set Material writes stock Generic material GUIDs; Import Material invokes the stock USB profile importer. The deeper stock brand/type/color browser is still richer. Deneb currently also exposes Import Material as a top-level shortcut. |
| Maintenance | Set nozzle temperature, update firmware, move build plate, level build plate, diagnostics/save logs | Mostly present. Temperature, Deneb package update from USB, move build plate, leveling macro steps, diagnostics telemetry, Air Manager fan toggle, and stock-shaped log export exist. Deneb intentionally does not install stock UltiMaker firmware updates. |
| Settings | Set nozzle size, network configuration, Digital Factory, frame lighting, factory reset, about | Mostly present. Nozzle size, network info, USB WiFi/Ethernet config import, Digital Factory status/disconnect/PIN pairing, frame lighting, factory reset, language, and about exist. |
| About | About, printer ID, certifications | Present. Deneb About shows version/project info, stock base version, printer ID, and stock certification text. |
| Startup/status | Welcome/update success, recovered print, faults, cloud state | Partial. Deneb has status and error display, but stock recovery/update-success/cloud flows are not yet matched. |

## Release-Critical Gaps

1. Stock-style WiFi setup progress animation

   Deneb replaces the stock AP/captive-portal wizard with USB `wifi.txt`
   import. This avoids the stock Tornado wificonnect service, but it does not
   reproduce the stock animated progress wizard.

2. Print preparation parity

   Stock has richer UFP/print preparation handling, including thumbnails and
   material/nozzle conflict pages. Deneb now requires selecting a file before
   starting it and handles mismatch continue/abort state transitions, but it
   still does not reproduce those richer stock flows.

## Implemented Native Replacements

1. Deneb update / install from USB

   Deneb's runtime update screen is Deneb-only. It scans common USB mount
   points for `.deneb` packages, extracts the selected package to `/tmp/update`,
   and runs its `update.sh`. It intentionally does not scan for or install
   UltiMaker `.img` stock firmware updates. A future GitHub release check can
   be added when private release access is ready.

2. Network configuration

   Deneb now displays printer/network address information and supports USB
   WiFi/Ethernet imports from `wifi.txt` and `eth.txt`. See
   [WiFi setup](WIFI_SETUP.md) and [Ethernet setup](ETH_SETUP.md).

3. Build plate leveling

   Deneb now exposes the stock `buildplate_level_step1-4.gcode` and
   `buildplate_level_finish.gcode` macro sequence.

4. Diagnostics / save logs

   Deneb now shows Air Manager presence, build-volume temperature, and an Air
   Manager fan toggle. Log export writes a stock-shaped
   `UM2C_<printer>_v<version>_<UTC>.tar.gz` containing log files plus a
   redacted `uci_dump`.

5. Material workflows

   Deneb now exposes all stock material menu entries. Load, unload, change, and
   move are backed by known stock macros/G-code. Set Material persists the same
   `ultimaker.option.material_guid` value as stock for Generic materials, and
   Import Material runs the stock USB material-profile importer.

6. Digital Factory pairing

   Deneb installs a small Gershwin IPC bridge and exposes Pair / Show PIN,
   and authenticated disconnect controls from the native Digital Factory screen.

7. Print-state controls

   Deneb now gates Stop by real print lifecycle state: Stop is disabled on an
   idle boot/home screen, enabled during preheat as well as active printing,
   and cleared after abort completion. Status returns from mismatch/continue
   and abort paths instead of staying stuck on stale Idle or Printing labels.

## Suggested Implementation Order

1. Expand the material selector from Generic shortcuts to the full stock
   brand/type/color database browser.
2. Add richer print preparation pages for thumbnails and material/nozzle
   conflicts.

## Resource Guardrail

Each added screen should stay within the current resource target. Recent live
measurements for the current safe build were about 2.7 MB VSZ / about 2 MB RSS
for `deneb-ui --lang en`, with a settled system CPU sample around 90% idle.
The stock menu baseline was about 33.7 MB VSZ / about 21 MB RSS. Treat these as
idle guardrail numbers until printing, update, diagnostics, and Digital Factory
flows have their own samples.
