# Deneb Firmware Audit

Date: 2026-05-31
Source: Live device SSH (10.10.10.244) + unpacked firmware recovery in `rootfs/`
Hardware: UltiMaker 2+ Connect (Onion Omega2+, MediaTek MT7688)
Printer hostname: Ultimaker-2C-9CF8

## Hardware Profile

| Component | Detail |
|-----------|--------|
| SoC | MediaTek MT7688 ver:1 eco:2 |
| CPU | MIPS 24KEc V5.5, 385.84 BogoMIPS, single core |
| Platform | Onion Omega2+ |
| RAM | 124,584 KB total (no swap) |
| Display | ILI9341 via SPI0, 320x240 RGB565 |
| Touch | FTS touchscreen (kernel driver, IRQ 40) |
| Storage | MMC + JFFS2 + EXT4 |

## Operating System

| Component | Installed Version | End-of-Life | Notes |
|-----------|------------------|-------------|-------|
| OpenWrt | 18.06-SNAPSHOT r7499-44c7d0a524 | Mid-2020 | ~6 years old, massive CVE exposure |
| OpenSSL | 1.0.2p (Aug 2018) | EOL since 2019 | Requires legacy SSH algorithm flags |
| Python | 3.6 | Dec 2021 | All stock services depend on it |
| BusyBox | 1.28.x-era | N/A (rolling) | Part of OpenWrt 18.06 |
| pip | 18.1 | Ancient | Cannot install from PyPI (TLS too old) |
| setuptools | 40.6.2 | Ancient | 2018-era |

## SSH Access Problem

OpenSSL 1.0.0 uses deprecated RSA-SHA1 host key exchange. Connecting requires:

```
ssh -o HostKeyAlgorithms=+ssh-rsa \
    -o PubkeyAcceptedAlgorithms=+ssh-rsa \
    root@<printer-ip>
```

The dropbear SSH key is `dropbear_rsa_host_key` (RSA only, no Ed25519).

SSH is disabled by default. The stock `97_disable_wifi_services` uci-defaults script
disables dropbear unless `ultimaker.version.channel` is `internal`.

## Stock Python Services (Memory Hogs)

| PID | Service | Script | VSZ (MB) | Notes |
|-----|---------|--------|----------|-------|
| 1174 | Menu/UI | executor.py | 33.7 | **REPLACED** by Deneb C UI (2.7 MB) |
| 1501 | Digital Factory | connector.py | 33.5 | Cloud pairing, WebSocket |
| 1129 | Coordinator | coordinator.py | 27.3 | Gershwin IPC node graph |
| 1124 | Print Service | print_service.py | 18.7 | Marlin serial driver |
| **Total** | **All Python** | | **113.2** | Total virtual address space; not physical RAM |

## Python Package Inventory

| Package | Version | Latest (2026) | CVE Status |
|---------|---------|---------------|------------|
| Twisted | 20.3.0 | 24.x | Multiple known CVEs |
| pyzmq | 19.0.1 | 26.x | Outdated |
| tornado | 6.0.4 | 6.4 | Outdated |
| lxml | 4.1.1 / 4.6.2 | 5.x | Multiple CVEs (libxml2) |
| gershwin | 1.0.1 | N/A | Proprietary UltiMaker IPC |
| sponge | N/A | N/A | Proprietary UltiMaker IPC |
| txZMQ | 0.8.2 | Latest | 2018-era |
| zope.interface | 5.5.2 | 6.x | Outdated |
| pyserial | 3.4 | 3.5 | Outdated |
| u-msgpack-python | 2.6.0 | 2.8 | Outdated |
| pip | 18.1 | 24.x | Cannot install packages |
| setuptools | 40.6.2 | 70.x | Ancient |
| Charon | N/A | N/A | Proprietary UltiMaker file service |
| stardustWebsocketProtocol | N/A | N/A | Proprietary UltiMaker DF protocol |

## Proprietary Libraries Bundled

| Library | Location | Files | Purpose |
|---------|----------|-------|---------|
| gershwin | site-packages/gershwin/ | 15 .py | UltiMaker IPC node/step framework |
| sponge | site-packages/sponge/ | 8 .py | UltiMaker IPC spinner/poller |
| Charon | home/lib/Charon/ | 20 .py | File service (GCode, UFP, OPC) |
| stardustWebsocketProtocol | home/lib/stardustWebsocketProtocol/ | 56 .py | Digital Factory WebSocket protocol |

These are NOT open source. Do not redistribute. The Deneb C replacements
speak the same wire protocols (ZMQ, Gershwin IPC, msgpack) but are original
clean-room implementations.

## Firmware Version Info

From `11_ultimaker` uci-defaults:
```
ultimaker.version.nr = 1.5.3
ultimaker.version.build = 2023-05-30
ultimaker.version.channel = stable (default)
```

## Package Feed Status

The opkg feeds point at `downloads.openwrt.org/releases/18.06-SNAPSHOT/`.
These feeds are likely dead/archived. Updating packages via opkg will probably
fail unless feeds are repointed to `archive.openwrt.org`.

## What Is Broken

1. **SSH requires legacy algorithm flags** (OpenSSL 1.0.0)
2. **Package feeds likely dead** (18.06-SNAPSHOT repos)
3. **CA certificates stale** (cannot verify modern TLS endpoints)
4. **pip cannot install from PyPI** (TLS handshake fails)
5. **Python 3.6 EOL** (no security patches since Dec 2021)
6. **Twisted/lxml have known CVEs** (exposed network services)
7. **Total Python VSZ (113 MB)** shows a large virtual-address footprint; use RSS measurements for physical-RAM pressure
8. **Print quality degrades** after extended uptime (documented in checklist)

## What Can Be Updated (Lower Risk)

These can potentially be updated via opkg if feeds are repointed, or replaced
with static C binaries:

- Dropbear SSH daemon
- dnsmasq (DNS/DHCP)
- CA certificate bundle
- busybox

## What Cannot Be Easily Updated (Would Break the Stack)

- OpenSSL 1.0.0 -- every binary links against it
- Python 3.6 -- all stock services depend on it
- OpenWrt 18.06 base -- kernel, procd, ubus, etc.
- Kernel -- tied to MT7688 SoC support

## Python -> C Replacement Candidates

### Phase 1: Deneb-authored code (can freely rewrite)

| Component | Lines | Complexity | Impact |
|-----------|-------|------------|--------|
| deneb-df-bridge.py | ~155 | LOW | Eliminates Python + gershwin + umsgpack + zmq |

### Phase 2: Stock service replacements (clean-room only)

| Component | Lines | Complexity | RAM Savings |
|-----------|-------|------------|-------------|
| print_service.py | ~500+ | MEDIUM-HIGH | ~19 MB VSZ |
| coordinator.py | ~183 main + 15 handlers | HIGH | ~27 MB VSZ |
| wificonnect/server.py | ~138 | LOW-MEDIUM | Eliminates Tornado |

### Phase 3: Longer term

| Component | Lines | Complexity | Notes |
|-----------|-------|------------|-------|
| connector.py | Large | VERY HIGH | Cloud-only feature, lowest priority |

## Deneb Progress So Far

- [x] Touchscreen UI replaced with LVGL v9 C (2.7 MB vs 33.7 MB = ~92% reduction)
- [x] SSH bootstrap package (Deneb_get_started.img)
- [x] Deneb .deneb package update system
- [x] Branding assets, splash screen
- [x] Digital Factory bridge C rewrite embedded in `deneb-ui`; `/usr/bin/deneb-df-bridge` is installed as a symlink entry point
- [ ] WiFi connect server C rewrite
- [ ] Print service C rewrite
- [ ] Coordinator C rewrite
- [ ] OS/service modernization

## Live Device Snapshot (2026-05-31, uptime ~1 hour)

Access via: `tools/deneb-ssh.py '<command>'` (uses paramiko, handles ssh-rsa)
Or: `plink -batch -pw deneb root@10.10.10.244 '<command>'`

### Running Processes and Memory

| PID | Process | VSZ (kB) | RSS (kB) | Notes |
|-----|---------|----------|----------|-------|
| 1091 | python3 (print_service.py) | 18,872 | 15,360 | Stock Marlin serial driver |
| 1096 | python3 (coordinator.py) | 27,616 | 22,564 | Stock coordinator |
| 1189 | deneb-ui | 2,748 | 1,568 | Deneb LVGL C UI |
| 1024 | dropbear | 1,064 | 808 | SSH daemon |
| 845 | rpcd | 1,788 | 956 | OpenWrt RPC daemon |
| 937 | netifd | 1,724 | 1,012 | Network interface daemon |
| 1047 | onion-helper | 1,968 | 1,208 | Onion helper service |

Stock menu (executor.py): NOT RUNNING (replaced by deneb-ui; pruned by Deneb installer after smoke test)
Digital Factory (connector.py): NOT RUNNING (starts async, disabled by default)
WiFi Connect server: NOT RUNNING (started on demand by stock menu)

Total remaining Python RSS: ~37.9 MB. The stock-service table above uses VSZ,
so compare VSZ-to-VSZ or RSS-to-RSS only.

### Memory State

```
MemTotal:    124,584 kB
MemFree:      44,488 kB
MemAvailable: 41,008 kB
Buffers:      11,312 kB
Cached:       27,592 kB
```

### Flash Storage

```
/dev/mtdblock6   10.9M   4.6M used   6.4M available (42%)
```

### Network Listeners

| Port | Service | PID |
|------|---------|-----|
| 22 | dropbear (SSH) | 1024 |
| 5546 | python3 (print_service Gershwin pub) | 1091 |
| 5548 | python3 (coordinator Gershwin pub) | 1096 |
| 5555 | python3 (print_service status PUB) | 1091 |
| 5556 | python3 (print_service command REP) | 1091 |
| 5565 | python3 (coordinator status PUB) | 1096 |
| 5566 | python3 (coordinator command REP) | 1096 |

### Package Feeds (all commented out, Onion repo only)

OpenWrt feeds are all commented out. Only Onion IoT repo is active:
- `http://repo.onioniot.com/omega2/packages/{core,base,packages,routing,onion}`

This means opkg updates require the Onion repo to be reachable.

### Key Binaries on Device

| Binary | Size | Type |
|--------|------|------|
| /usr/bin/deneb-ui | ~1.8 MB stripped package binary | ELF MIPS (static, musl) with embedded C Digital Factory bridge |
| /usr/bin/deneb-df-bridge | symlink | Entry point to `deneb-ui` bridge mode |

### Stock Menu Tree

`/home/cygnus/menu` is about 884 KiB in the recovery image. `executor.py` is
the stock touchscreen UI entry point. Deneb can remove the dormant UI files,
but must retain `menu_settings.py` and `machine_config.json` because non-UI
stock services import shared paths and endpoint constants from
`cygnus.menu.menu_settings`.

### SSH Access Helper

File: `tools/deneb-ssh.py`

Usage: `python tools/deneb-ssh.py '<command>'`

Uses paramiko with ssh-rsa monkey-patch to handle the ancient Dropbear.
Needs Python 3.13 with paramiko installed (`pip install paramiko`).

Alternative: `/c/Program Files/PuTTY/plink -batch -pw deneb root@10.10.10.244 '<command>'`
