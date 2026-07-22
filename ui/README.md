# Deneb Touchscreen UI

Replacement touchscreen UI for the UltiMaker 2+ Connect.
Replaces the stock Python/Cygnus menu with a lightweight native LVGL C implementation.

## Target Hardware

- MT7688 MIPS 24KEc (OpenWrt / Onion Omega2+)
- ILI9341 320x240 SPI TFT (RGB565, /dev/fb0)
- FTS resistive touchscreen (evdev /dev/input/event0)
- 124 MB RAM

## Touch Interaction

The stock touchscreen firmware accepted discrete taps only. Deneb adds direct
drag interaction in both axes:

- Vertical swipe-to-scroll for menus, file lists, and content extending beyond
  the 320x240 viewport.
- Horizontal drag sliders for nozzle and bed temperature, material-workflow
  temperature, and frame-light brightness.

The target evdev driver samples touch input every 8 ms, begins a scroll after
6 pixels of movement, and uses a small bounded throw suited to the resistive
panel. Scrollable views show an automatic scrollbar with elastic overscroll and
momentum disabled.

These gestures manipulate content and controls. They do not provide global
left/right screen navigation; changing screens still uses visible buttons and
the Back control.

## Resource Evidence

UI-specific and whole-stack measurements are retained in
[baseline evidence](../docs/evidence/BASELINE_MEASUREMENTS.md). In particular,
the older post-menu-disable Python total is an intermediate UI-replacement
snapshot, not the current whole-stack result. Keep new UI work bounded and
remeasure the complete system across every affected workflow rather than
treating an old component snapshot as a permanent budget.

## Architecture

```
ui/
  CMakeLists.txt                        Build system
  cmake/
    mipsel-musl-toolchain.cmake         Production toolchain (musl)
    mipsel-linux-gnu-toolchain.cmake    Alternative (glibc)
    mips-openwrt-toolchain.cmake        OpenWrt SDK toolchain
  config/
    lv_conf.h                           LVGL v9 config (320x240, 48KB heap)
  init/
    deneb-ui.init                       OpenWrt procd init script
  installer/
    update.sh                           .deneb package installer
  locales/
    en.json, nl.json, de.json, fr.json  Translations
    zh-Hans.json, en-pirate.json, en-1337.json
  lib/
    lvgl/                               LVGL v9.6.0-dev pinned git submodule
  src/
    main.c                              Entry point, main loop
    app_fonts.c / app_fonts.h           Montserrat + generated i18n fallbacks
    locale.c / locale.h                 JSON i18n loader
    screen_mgr.c / screen_mgr.h         Navigation stack
    backend_comm.c / backend_comm.h     ZMQ IPC client
    net_utils.c / net_utils.h           Shared USB network config helpers
    wifi_setup.c / wifi_setup.h         wifi.txt import and UCI apply logic
    eth_setup.c / eth_setup.h           eth.txt import and UCI apply logic
    drivers/
      fb_driver.c / fb_driver.h         /dev/fb0 mmap (Linux target)
      touch_driver.c / touch_driver.h   evdev touchscreen (Linux target)
      fb_driver_stub.c                  Host build stub
      touch_driver_stub.c               Host build stub
    screens/
      screen_home.c                     Main menu
      screen_status.c                   Live status from coordinator
      screen_print.c                    USB file browser + print control
      screen_material.c                 Material workflows
      screen_maintenance.c              Maintenance menu
      screen_jog.c                      X/Y/Z jogging + bed control
      screen_temp.c                     Temperature control + cooldown
      screen_error.c                    ER code display
      screen_settings.c                 Settings menu
      screen_language.c                 Language selector
      screen_update.c                   Deneb package updater
      screen_network.c                  Network status/setup actions
      screen_digital_factory.c          Digital Factory controls
      screen_level.c                    Build plate leveling macros
      screen_diagnostics.c              Diagnostics/log export
      screen_frame_lighting.c           Frame lighting controls
      screen_nozzle_size.c              Nozzle size setting
      screen_factory_reset.c            Factory reset confirmation
      screen_set_material.c             Material selection/import
      screen_about.c                    Version, license, credits
    fonts/
      deneb_font_i18n_*.c               Generated exact-subset locale fonts
  build-package.sh                      Build .deneb update package
  wifi.txt.example                      Example WiFi USB import file
  eth.txt.example                       Example Ethernet USB import file
  README.md                             This file
```

## Build Prerequisites

### Production build (MIPS musl)

Requires the complete Debian/WSL 2 environment in
[../docs/WSL_BUILD_ENVIRONMENT.md](../docs/WSL_BUILD_ENVIRONMENT.md), including
the current root-default-user constraint and the manually bootstrapped static
mbedTLS tree. The abbreviated commands below are not sufficient on a fresh
machine by themselves.

```bash
# First run only: download musl cross-compiler
wsl -d Debian -- bash -c 'cd /tmp && curl -sL -o musl-cross.tar.gz \
  "https://musl.cc/mipsel-linux-musl-cross.tgz" && \
  tar xzf musl-cross.tar.gz && \
  cp -r mipsel-linux-musl-cross ~/mipsel-linux-musl-cross'

# First run only: fetch/build release dependencies
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1 -RebuildZmq -RebuildLighttpd

# Later builds
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```

### Host build (for code testing, no display)

Host builds are WSL/Linux builds with stub drivers. Visual Studio/MSVC is not a
supported toolchain for Deneb C targets.

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-ui-host.ps1
```

## Installation

1. Copy the latest `dist/Deneb_Update_<commit>.deneb` package to a USB drive
2. Insert USB into the UltiMaker 2+ Connect
3. On Deneb: Maintenance > Update Firmware
4. Select the Deneb update .deneb file
5. Wait for installation and reboot

The installer will:
- Back up the stock menu init script
- Disable the stock Cygnus menu (S96)
- Install the Deneb UI, web/API runtime, init scripts, and locales. Digital
  Factory touchscreen actions are handled by the local `deneb-api
  digital-factory` command mode.
- Patch the stock coordinator ZMQ poll-state issue that can pin CPU after updates
- Disable the stock WiFi AP/captive portal, remove the obsolete stock web
  assets from the live filesystem view with overlayfs, disable AP-side
  DHCP/DNS/IPv6 server services without removing their read-only base-image
  binaries, and replace setup with USB import
- Reboot into the new UI

## Network Setup

Deneb configures networking from USB files instead of the stock WiFi captive
portal. On the touchscreen, open Settings > Network, insert a USB drive, then
choose the relevant import action.

- Use [WiFi setup via USB](../docs/WIFI_SETUP.md) with `wifi.txt` to configure
  WiFi SSID, password, DHCP/static IP, DNS, NTP, hostname, and country.
- Use [Ethernet setup via USB](../docs/ETH_SETUP.md) with `eth.txt` to set
  Ethernet DHCP/static IP, DNS, NTP, or hostname.
- Template files live at [`wifi.txt.example`](wifi.txt.example) and
  [`eth.txt.example`](eth.txt.example).

## Backend IPC

The UI communicates with native `deneb-printsvc` via ZeroMQ:
- Status: SUB on tcp://127.0.0.1:5555, topic "10001"
- Commands: REQ on tcp://127.0.0.1:5556

Command format: `COMMAND<json_payload`
- `GCODE<["M140 S60"]` - Send G-code
- `MACRO<{"macro":"home_and_center_head.gcode"}` - Run macro
- `JOB<{"file":"/mnt/sda1/file.gcode","source":"USB","uuid":"0"}` - Start print
- `PAUSE<{} / RESUME<{} / ABORT<{} - Print control

See docs/BACKEND_IPC_PROTOCOL.md for full details.

## Digital Factory Bridge

Deneb uses the existing `/usr/bin/deneb-api digital-factory` local command mode
for UI-side Digital Factory status, connect, and disconnect actions. This keeps
the connector logic out of `deneb-ui` while reusing the C web/API binary that
already links the coordinator ZeroMQ stack. No Deneb-owned Python Digital
Factory bridge and no standalone `deneb-df-bridge` binary are packaged or kept
as runtime fallbacks.

This is not a new Web or cloud API endpoint. The command uses the retained
coordinator-compatible Digital Factory IPC contract while the native
`deneb-dfsvc` owns active connector behavior. It does not require a live stock
Python coordinator or helper.

Active Digital Factory cloud connectivity is handled by the native
`deneb-dfsvc` service installed as `/etc/init.d/digitalfactory`. The touchscreen
screen uses one primary `Connect` action to enable/start that service and invoke
the bridge connect request; when the status is connected, reconnecting,
connecting, enter-pin, or disconnecting, it enables the guarded `Disconnect`
path. Disconnect is a two-tap action and stops/disables the service after the
cluster state is cleared.

The installer replaces the stock Python `digitalfactory` init path with the
native service and disables it when no `ultimaker.option.cluster_id` is
configured. Package/native audits reject Python Digital Factory bridge
artifacts and stock connector fallback launches. Pairing, reconnect,
disconnect, rename, representative remote printing, and print actions have
bounded target evidence for named packages; broader client and soak coverage
remains open in [PROJECT_STATUS.md](../docs/PROJECT_STATUS.md).

## Screen Navigation

```
Home (Deneb)
  +-- Status        (live temps, progress, position)
  +-- Print from USB (file browser, select/review/start, pause/resume/cancel)
  +-- Material      (change/load/unload/set/move/import entry points)
  +-- Maintenance
        +-- Temperature
        +-- Update Firmware (Deneb .deneb packages only)
        +-- Move Build Plate
        +-- Level Build Plate
        +-- Diagnostics
  +-- Manual Control (X/Y/Z jog, bed up/down, home)
  +-- Temperature   (nozzle/bed set, cooldown)
  +-- Settings
        +-- Language (EN/NL/DE/FR/ZH-Hans/Pirate/1337)
        +-- Set Nozzle Size
        +-- Network
              +-- Hostname
              +-- WiFi status
              +-- Ethernet status
              +-- WiFi on/off switch
              +-- Load WiFi Settings from USB
              +-- Load Ethernet Settings from USB
              +-- Reset Ethernet to DHCP
        +-- Digital Factory
        +-- Frame Lighting
        +-- Factory Reset
        +-- About Deneb
```

The current UI now covers the major stock menu areas while keeping the update
lane Deneb-only. See `docs/STOCK_UI_COVERAGE.md` for the current stock-vs-Deneb
coverage matrix, including the stock base version shown on the About screen and
the remaining native LVGL replacement work.

For visual reference, see the screenshot-backed
[`docs/TOUCHSCREEN_SCREEN_CATALOG.md`](../docs/TOUCHSCREEN_SCREEN_CATALOG.md).

## Locale Support

JSON locale files in /etc/deneb/locales/ on the device.
Supported: en, nl, de, fr, zh-Hans, en-pirate, en-1337.
Set via Settings > Language or: `uci set deneb.system.language=de && uci commit deneb`

Translated UI text uses generated LVGL font subsets for non-ASCII glyphs used
by the bundled locale files. Regenerate those fonts whenever locale text adds
new non-ASCII characters.

## License

MPL-2.0 (original Deneb code). LVGL is MIT licensed. libzmq is MPL-2.0.
