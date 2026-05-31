# Deneb Baseline Measurements

Date: 2026-05-30
Source: Live UltiMaker 2+ Connect (10.10.10.244)
Firmware: Stock Cygnus

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

| Build | Toolchain | Stripped | Notes |
|-------|-----------|----------|-------|
| musl (production) | mipsel-linux-musl-gcc 11.2.1 | 2.0 MB | Static, musl libc |
| glibc | mipsel-linux-gnu-gcc 14.2.0 | 2.5 MB | Static, glibc |
| host (testing) | gcc 13.2.0 (Windows) | 1.5 MB | Stub drivers, no ZMQ |

## Comparison

| Metric | Stock (measured) | Deneb | Reduction |
|--------|-----------------|-------|-----------|
| Menu binary | N/A (Python) | 2.0 MB | N/A |
| Menu RAM (VSZ) | 33.7 MB | ~0.3 MB (est.) | 99% |
| All Python RAM | 113.2 MB | ~80 MB (est.) | 29% |
| ZMQ ports | Same | Same | Compatible |

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
