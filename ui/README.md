# Deneb Touchscreen UI

Replacement touchscreen UI for the UltiMaker 2+ Connect.
Replaces the stock Python/Cygnus menu with a lightweight native LVGL C implementation.

## Target Hardware

- MT7688 MIPS 24KEc (OpenWrt / Onion Omega2+)
- ILI9341 320x240 SPI TFT (RGB565, /dev/fb0)
- FTS resistive touchscreen (evdev /dev/input/event0)
- 124 MB RAM

## Resource Comparison

| Metric | Stock (Python) | Deneb (LVGL C) | Reduction |
|--------|---------------|-----------------|-----------|
| Runtime package | N/A (Python app tree) | ~8.6 MiB `.deneb` | N/A |
| Menu RAM (VSZ) | 33.7 MB | 2.7 MB measured | ~92% |
| Menu RAM (RSS) | ~21 MB measured | ~2 MB measured | ~90% |
| All Python service VSZ | 113.2 MB | ~79.5 MB after stock menu disable | ~30% |
| Settled idle CPU | Stock baseline still being normalized | ~90% idle system sample | In progress |
| IPC backend | ZMQ + Python | ZMQ + C | Same protocol |

The Deneb measurements are from a live idle printer sample after the stock menu
was disabled and `deneb-ui --lang en` was running. CPU numbers are a snapshot,
not yet a full benchmark across printing, update, and diagnostic workflows.

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

Requires WSL with Debian:

```bash
# First run only: download musl cross-compiler
wsl -d Debian -- bash -c 'cd /tmp && curl -sL -o musl-cross.tar.gz \
  "https://musl.cc/mipsel-linux-musl-cross.tgz" && \
  tar xzf musl-cross.tar.gz && \
  cp -r mipsel-linux-musl-cross ~/mipsel-linux-musl-cross'

# First run only: cross-compile libzmq
wsl -d Debian -- bash -c 'cd /tmp && \
  curl -sL -o zeromq-4.3.5.tar.gz \
  https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.tar.gz && \
  tar xzf zeromq-4.3.5.tar.gz && \
  cd zeromq-4.3.5 && mkdir build-musl && cd build-musl && \
  cmake .. -DCMAKE_TOOLCHAIN_FILE=/mnt/c/temp/Deneb/ui/cmake/mipsel-musl-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DWITH_LIBSODIUM=OFF -DZMQ_BUILD_TESTS=OFF \
    -DWITH_DOCS=OFF -DBUILD_SHARED=OFF -DBUILD_STATIC=ON && \
  make -j$(nproc)'

# Build the UI
wsl -d Debian -- bash -c 'cd /mnt/c/temp/Deneb/ui && \
  mkdir -p build-musl && cd build-musl && \
  cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mipsel-musl-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DZMQ_LIBRARY=/tmp/zeromq-4.3.5/build-musl/lib/libzmq.a \
    -DZMQ_INCLUDE_DIR=/tmp/zeromq-4.3.5/include && \
  make -j$(nproc)'

# Build .deneb package
wsl -d Debian -- bash -c 'cd /mnt/c/temp/Deneb && \
  bash ui/build-package.sh ui/build-musl/deneb-ui'
```

### Host build (for code testing, no display)

```bash
cd ui && mkdir build-host && cd build-host
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DBACKEND_COMM_STUB=ON
ninja
```

## Installation

1. Copy the latest `dist/Deneb_UI_<commit>.deneb` package to a USB drive
2. Insert USB into the UltiMaker 2+ Connect
3. On Deneb: Maintenance > Update Firmware
4. Select the Deneb UI .deneb file
5. Wait for installation and reboot

The installer will:
- Back up the stock menu init script
- Disable the stock Cygnus menu (S96)
- Install the Deneb UI binary, init script, locales, and Digital Factory bridge
- Patch the stock coordinator ZMQ poll-state issue that can pin CPU after updates
- Disable the stock WiFi AP/captive portal and replace it with USB import
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

The UI communicates with the stock coordinator via ZeroMQ:
- Status: SUB on tcp://127.0.0.1:5565, topic "10001"
- Commands: REQ on tcp://127.0.0.1:5566

Command format: `COMMAND<json_payload`
- `GCODE<["M140 S60"]` - Send G-code
- `MACRO<{"macro":"home_and_center_head.gcode"}` - Run macro
- `JOB<{"file":"/mnt/sda1/file.gcode","source":"USB","uuid":"0"}` - Start print
- `PAUSE<{} / RESUME<{} / ABORT<{} - Print control

See docs/BACKEND_IPC_PROTOCOL.md for full details.

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
              +-- WiFi status
              +-- Import WiFi from USB
              +-- Disconnect WiFi
              +-- Ethernet status
              +-- Import Ethernet from USB
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

## Locale Support

JSON locale files in /etc/deneb/locales/ on the device.
Supported: en, nl, de, fr, zh-Hans, en-pirate, en-1337.
Set via Settings > Language or: `uci set deneb.system.language=de && uci commit deneb`

Translated UI text uses generated LVGL font subsets for non-ASCII glyphs used
by the bundled locale files. Regenerate those fonts whenever locale text adds
new non-ASCII characters.

## License

MPL-2.0 (original Deneb code). LVGL is MIT licensed. libzmq is MPL-2.0.
