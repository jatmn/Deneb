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
| Binary size | N/A (Python) | 2.0 MB | N/A |
| Menu RAM (VSZ) | 33.7 MB | ~0.3 MB (est.) | 99% |
| All Python RAM | 113.2 MB | ~80 MB (est.) | 29% |
| IPC backend | ZMQ + Python | ZMQ + C | Same protocol |

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
    lvgl/                               LVGL v9.6.0-dev (git submodule)
  src/
    main.c                              Entry point, main loop
    locale.c / locale.h                 JSON i18n loader
    screen_mgr.c / screen_mgr.h         Navigation stack
    backend_comm.c / backend_comm.h     ZMQ IPC client
    drivers/
      fb_driver.c / fb_driver.h         /dev/fb0 mmap (Linux target)
      touch_driver.c / touch_driver.h   evdev touchscreen (Linux target)
      fb_driver_stub.c                  Host build stub
      touch_driver_stub.c               Host build stub
    screens/
      screen_home.c                     Main menu (6 items)
      screen_status.c                   Live status from coordinator
      screen_print.c                    USB file browser + print control
      screen_material.c                 Material load/unload
      screen_jog.c                      X/Y/Z jogging + bed control
      screen_temp.c                     Temperature control + cooldown
      screen_error.c                    ER code display
      screen_settings.c                 Language selector
      screen_about.c                    Version, license, credits
  build-package.sh                      Build .deneb update package
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

1. Copy `dist/Deneb_UI_ae7588b.deneb` to a USB drive
2. Insert USB into the UltiMaker 2+ Connect
3. On the touchscreen: Settings > Update firmware > Install from USB
4. Select the Deneb UI .deneb file
5. Wait for installation and reboot

The installer will:
- Back up the stock menu init script
- Disable the stock Cygnus menu (S96)
- Install the Deneb UI binary + init script + locales
- Reboot into the new UI

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
  +-- Print from USB (file browser, start/pause/resume/cancel)
  +-- Material      (load/unload via macros)
  +-- Manual Control (X/Y/Z jog, bed up/down, home)
  +-- Temperature   (nozzle/bed set, cooldown)
  +-- Settings
        +-- Language (EN/NL/DE/FR/ZH-Hans/Pirate/1337)
        +-- About Deneb
```

## Locale Support

JSON locale files in /etc/deneb/locales/ on the device.
Supported: en, nl, de, fr, zh-Hans, en-pirate, en-1337.
Set via Settings > Language or: `uci set deneb.system.language=de && uci commit deneb`

## License

MPL-2.0 (original Deneb code). LVGL is MIT licensed. libzmq is LGPL.
