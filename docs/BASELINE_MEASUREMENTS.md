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
| musl (production) | mipsel-linux-musl-gcc 11.2.1 | ~1.8 MiB stripped | Static, musl libc, LVGL, ZMQ, generated i18n fonts; Digital Factory UI actions call `deneb-api digital-factory` |
| glibc | mipsel-linux-gnu-gcc 14.2.0 | 2.5 MB | Static, glibc |
| host (testing) | gcc under WSL/Linux | 1.5 MB | Stub drivers, no ZMQ |

## Digital Factory Bridge Footprint

Measured on hardware after installing `dist/Deneb_Update_0e8e562.deneb` on
2026-06-11. The package is 5,703,680 bytes and does not include a standalone
Digital Factory bridge binary. UI Digital Factory actions use the existing C
`deneb-api digital-factory` command mode, which keeps steady-state bridge
footprint at zero when no action is running.

| Artifact / run | Size or peak | Notes |
|----------------|--------------|-------|
| `/usr/bin/deneb-ui` | 1,862,180 bytes | Installed stripped UI binary |
| `/usr/bin/deneb-api` | 1,410,108 bytes | Installed stripped API binary with local `digital-factory` command mode |
| Current `deneb-api digital-factory status --timeout 10` | 2,368 kB peak VSZ / 1,036 kB peak RSS | Returned `status=timeout` with rc 0 on an unpaired printer; reuses the existing API binary and stock Gershwin coordinator IPC |
| Idle after command exit | no bridge process | No lingering bridge process after the one-shot status action |
| Stock `digitalfactory` boot gate | disabled when unpaired | With no `ultimaker.option.cluster_id`, `/etc/init.d/digitalfactory enabled` returned rc 1 |

### Standalone Bridge And Shared ZMQ Probe

Before settling on `deneb-api digital-factory`, a standalone MIPS musl bridge
was measured and rejected because it duplicated runtime weight already present
in the API binary.

| Probe | Size or peak | Result |
|-------|--------------|--------|
| Standalone static `deneb-df-bridge` | 1,711,428 bytes installed; 19,972 kB peak VSZ / 1,568 kB peak RSS for `status --timeout 10` | Rejected in favor of `deneb-api digital-factory` |
| Scratch shared `libzmq.so.5.2.5` plus dynamic Zig bridge | 2,112,196 bytes + 226,804 bytes = 2,339,000 bytes | Larger than the standalone static bridge for this use |

Deneb still ships statically linked native ZMQ consumers. Sharing ZMQ across
`deneb-ui`, `deneb-api`, and `deneb-printsvc` may become a package-size win,
but it is not a drop-in change: scratch dynamic C builds hit MIPS non-PIC
relocation errors, and the dynamic Zig probe requested
`/lib/ld-musl-mipsel.so.1` while the printer exposes
`/lib/ld-musl-mipsel-sf.so.1`.

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
UI smoke test succeeds, while retaining only the files still required by
unreplaced stock services.

**Retained files:**

| File | Dependency | Stock Services |
|------|------------|----------------|
| `menu_settings.py` | Shared path/endpoint constants | `coordinator/coordinator.py` (GCODE_DIR), `coordinator/handlers/filehandling.py` (GCODE_FILE), `coordinator/handlers/firmwareupdatehandling.py` (FW_IMG_COPY_TARGET, FW_IMG_EXTRACT_DIR), `coordinator/handlers/printhandling.py` (GCODE_FILE), `util/host_id.py` (LAN_INTERFACE), `util/network.py` (LAN_INTERFACE, WLAN_INTERFACE), `util/ufp_format.py` (GCODE_DIR) |
| `machine_config.json` | Machine geometry constants | Referenced by `menu_settings.MACHINE_CONFIG` path constant; retained defensively |

**Pruned directories (dormant UI):** `gui_companion/`, `helpers/`, `img/`,
`navigator/`, `screens/`, `templates/`, `ui_elements/`

**Pruned top-level files:** `executor.py`, `controldialog.py`, `machine.py`,
`pylvgl.py`, `screen.py`, `style.py`

The prune boundary is verified at build time by
`deneb-stock-menu-prune-selftest` and the retained constant dependency map is
checked by `deneb-stock-menu-import-check`. See
[`tools/deneb-stock-menu-prune-selftest.sh`](../tools/deneb-stock-menu-prune-selftest.sh)
and
[`tools/deneb-stock-menu-import-check.sh`](../tools/deneb-stock-menu-import-check.sh).

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
| build-host | gcc under WSL/Linux | Code testing, stub drivers |
| build-mips | mipsel-linux-gnu-gcc | Alternative glibc build |
| build-musl | mipsel-linux-musl-gcc | **Production build** |
