# Deneb Baseline Measurements

Date: 2026-05-30 baseline, updated 2026-05-31 with Deneb UI measurements
Source: Live UltiMaker 2+ Connect (10.10.10.244)
Firmware: Stock Cygnus baseline plus Deneb UI test builds

## Hardware

- SoC: MediaTek MT7688 ver:1 eco:2
- CPU: MIPS 24KEc V5.5, 385.84 BogoMIPS, single core
- Platform: Onion Omega2+
- RAM: 124,584 KB total
- Touch: FTS touchscreen (kernel driver, IRQ 40)
- Display: ILI9341 via SPI0, 320x240 RGB565
- Storage: MMC + JFFS2 + EXT4

## Memory (stock firmware idle, no active print)

```
             total       used       free     shared    buffers     cached
Mem:        124584     102556      22028         76       9292      18552
-/+ buffers/cache:      74712      49872
Swap:            0          0          0
```

## Process Memory (stock Python services)

| PID | Service | VSZ (KB) | VSZ (MB) |
|-----|---------|----------|----------|
| 1174 | Menu/UI (executor.py) | 34,552 | 33.7 |
| 1501 | Digital Factory (connector.py) | 34,328 | 33.5 |
| 1129 | Coordinator (coordinator.py) | 27,964 | 27.3 |
| 1124 | Print Service (print_service.py) | 19,120 | 18.7 |
| **Total** | **All Python** | **115,964** | **113.2** |

## Deneb UI Binary Sizes

| Build | Toolchain | Size | Notes |
|-------|-----------|----------|-------|
| musl (production) | mipsel-linux-musl-gcc 11.2.1 | ~1.8 MiB stripped | Static, musl libc, LVGL, ZMQ, generated i18n fonts, embedded C Digital Factory bridge |
| glibc | mipsel-linux-gnu-gcc 14.2.0 | 2.5 MB | Static, glibc |
| host (testing) | gcc 13.2.0 (Windows) | 1.5 MB | Stub drivers, no ZMQ |

The earlier 8.6 MiB package number included unstripped MIPS debug info. Release
packaging now strips the staged binary, and the current packaged release
artifact was about 1.8 MiB before the web runtime was bundled
(`Deneb_Update_<commit>.deneb` is the update release lane).

## Deneb UI Live Idle Snapshot

Measured after installing a current Deneb UI build, disabling the stock Cygnus
menu service, and letting the printer settle at the main UI.

| Process | VSZ (KB) | VSZ (MB) | RSS (MB, approx.) | Notes |
|---------|----------|----------|-------------------|-------|
| deneb-ui --lang en | 2,728 | 2.7 | ~2 | Native LVGL UI, framebuffer/touch drivers, ZMQ client |

Settled system CPU sample: about 90% idle. This is a useful sanity check, but
not a full benchmark. Printing, Deneb package updates, diagnostics export,
Digital Factory pairing, and language switching still need separate CPU/RAM
samples before release gates should rely on the numbers.

## Comparison

| Metric | Stock (measured) | Deneb | Reduction |
|--------|-----------------|-------|-----------|
| Menu binary/package | N/A (Python app tree) | ~1.8 MiB package | N/A |
| Menu RAM (VSZ) | 33.7 MB | 2.7 MB measured | ~92% |
| Menu RAM (RSS) | ~21 MB measured | ~2 MB measured | ~90% |
| All Python service VSZ | 113.2 MB | ~79.5 MB after stock menu disable | ~30% |
| ZMQ ports | Same | Same | Compatible |

## Stock Menu Files

`/etc/init.d/menu` starts `/home/cygnus/menu/executor.py`, which is the stock
Python touchscreen UI. The extracted stock menu tree is about 884 KiB on disk.
Deneb installer packages prune the dormant UI implementation after the native
UI smoke test succeeds, while retaining `menu_settings.py` and
`machine_config.json` because coordinator, file handling, firmware update
handling, host ID, network, and UFP utilities still import shared constants
from `cygnus.menu.menu_settings`.

## IPC (from live device)

- Protocol: ZeroMQ (ZMQ) localhost TCP
- Status SUB: tcp://127.0.0.1:5565, topic "10001"
- Command REQ: tcp://127.0.0.1:5566
- Status JSON: headTcur/set, bedTcur/set, X/Y/Z/E, file, Ttot/Tleft
- Commands: GCODE, MACRO, JOB, ABORT, PAUSE, RESUME
- Macro path: /home/cygnus/marlindriver/gcode/

## Build Targets

| Target | Toolchain | Purpose |
|--------|-----------|---------|
| build-host | gcc (Windows) | Code testing, stub drivers |
| build-mips | mipsel-linux-gnu-gcc | Alternative glibc build |
| build-musl | mipsel-linux-musl-gcc | **Production build** |
