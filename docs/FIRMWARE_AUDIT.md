# Deneb Firmware Audit

Date: 2026-05-31, updated 2026-06-01 after WiFi web asset cleanup and live resource sampling.
Status reconciliation: 2026-06-13 against commits through `caa1490`.
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

## Python -> C Replacement Status

### Phase 1: Deneb-authored code (can freely rewrite)

| Component | Current status | Evidence boundary |
|-----------|----------------|-------------------|
| `deneb-df-bridge.py` | Done | Removed from Deneb source/runtime; replaced by local `deneb-api digital-factory` command mode and package/audit gates that reject the Python bridge. |

### Phase 2: Stock service replacements (clean-room only)

| Component | Current status | Evidence boundary |
|-----------|----------------|-------------------|
| `print_service.py` | Native implementation/package path exists as experimental `deneb-printsvc` | Strong bounded hardware, host test, package, and no-Python route evidence exists in `docs/PRINTSVC_EVIDENCE_LEDGER.md`. Promotion is still blocked by LCD/Web/Cura/Digital Factory client workflows, representative slicer output, and long active-soak memory behavior. |
| `connector.py` | Native implementation/package path exists as `deneb-dfsvc` | Commit `1415245` adds the native connector, init replacement, package staging, and native audit gates. `docs/DF_LIFECYCLE_CLASSIFICATION.md` records 2026-06-13 hardware proof for pairing PIN, connected steady-state, reconnect after controlled cloud interruption, touchscreen disconnect, printer rename, and remote print material-mismatch wait-user-action/Cancel handling without stock `connector.py`. Safe Continue/start remains blocked by native `G280`/homing parity, and print-job action proof remains open. |
| `coordinator.py` | Deneb client routes source-obsoleted; stock fallback still bootable | Source audit shows Deneb LCD, Web/API, Cura/UM print-job, temperature, material, leveling, and proven Digital Factory routes no longer select the stock coordinator proxy. The remaining work is to retire or gate the installer/runtime coordinator fallback handoff, with rollback and workflow proof, rather than treating this as an undefined whole-coordinator rewrite. |
| `wificonnect/server.py` | Avoided for Deneb setup | Deneb uses USB `wifi.txt` import and disables/hides stock AP/captive-portal paths where applicable. This is containment/avoidance, not deletion from read-only firmware. |

### Phase 3: Longer term

No longer tracked here. The Digital Factory connector is part of the active
de-Python service-replacement work, not a feature-removal task.

## Deneb Progress So Far

- [x] Touchscreen UI replaced with LVGL v9 C (2.7 MB vs 33.7 MB = ~92% reduction)
- [x] SSH bootstrap package (Deneb_get_started.img)
- [x] Deneb .deneb package update system
- [x] Branding assets, splash screen
- [x] Digital Factory bridge moved out of `deneb-ui`; `deneb-api digital-factory`
  handles local status, connect, and disconnect commands without adding a
  web/cloud endpoint
- [x] Deneb-owned Python Digital Factory bridge removed from source and blocked
  from release packages by the native de-Python audit
- [x] Stock `digitalfactory` service is disabled at install when no
  `ultimaker.option.cluster_id` is configured, then enabled/started by the
  Digital Factory screen when the user begins pairing
- [x] Stock `connector.py` Digital Factory cloud connector has a Deneb-owned C
  replacement path (`deneb-dfsvc`) in source, package staging, and native init
  replacement. Pairing, connected, reconnecting, disconnect, rename, and failed
  remote-print request receipt now have live hardware evidence without a stock
  Python connector fallback. Remote print now also downloads, extracts UFP
  payloads, enters the material-mismatch wait-user-action path, and clears on
  Cancel. Continue/start after prompt is reopened as unsafe until native
  `G280`/homing behavior is stock-equivalent. Print-job actions remain open
  before calling this fully proven.
- [x] WiFi setup replacement: USB `wifi.txt` import, stock AP/captive portal disabled by installer
- [x] Lightweight web runtime bundled into Deneb update releases: `lighttpd`,
  `deneb-api`, static web UI, and `deneb-mdns`
- [x] Web/API controls for status, current job, pause/resume/cancel/stop,
  manual heat/cooldown, and guarded X/Y/Z motion
- [x] Cura mDNS discovery and local cluster API compatibility surface for
  printer status, materials, upload/start, pending-job visibility, and basic
  job actions
- [x] Deneb Cura network discovery plugin package for mapping `deneb_um2c` to
  Cura's stock UM2+ Connect machine profile
- [x] Touchscreen print-state fixes for boot idle Stop state, preheat Stop
  availability, mismatch continue flow, and abort cleanup/status handling
- [x] Print service C rewrite implementation/package track exists as
  experimental `deneb-printsvc`; promotion remains blocked by the proof gates in
  `docs/PRINTSVC_EVIDENCE_LEDGER.md`.
- [x] Digital Factory connector C rewrite implementation/package track exists
  as `deneb-dfsvc`; live cloud validation remains open in
  `docs/DF_LIFECYCLE_CLASSIFICATION.md`.
- [ ] Coordinator C rewrite
- [ ] OS/service modernization

## Live Device Snapshot (2026-05-31, uptime ~1 hour)

The live snapshots below are historical baseline evidence. They predate the
native print-service and native Digital Factory connector package paths, so they
must not be read as current Deneb process ownership. Use
`docs/PRINTSVC_EVIDENCE_LEDGER.md`, `docs/DF_LIFECYCLE_CLASSIFICATION.md`, and
fresh target samples for current runtime claims.

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
WiFi Connect server: HIDDEN from the live filesystem view by Deneb installer
after the native UI smoke test.
Stock WiFi React assets and asset hash: HIDDEN from the live filesystem view by
Deneb installer commit `ec32a4a`.
Nodogsplash captive-portal htdocs: HIDDEN from the live filesystem view by the
Deneb installer.
AP-side DHCP/DNS and IPv6 router-advertisement services: DISABLED by the Deneb
installer because Deneb networking is client-only. IPv4 client networking is
handled by `netifd` and `udhcpc`; the stale `dhcp.wlan` AP DHCP scope is
removed. The `dnsmasq` and `odhcpd` binaries remain in the read-only base image.
WiFi setup is handled by USB `wifi.txt` import; see
[WiFi setup via USB](WIFI_SETUP.md).

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
/dev/mtdblock6   10.9M   1.5M used   9.5M available (13%)
```

The hidden stock WiFi portal files originate in the read-only squashfs lower
layer. The `.deneb` installer hides them with overlayfs whiteouts, which removes
runtime visibility and reachability but does not reclaim squashfs/base-image
flash space. Service binaries such as `dnsmasq` and `odhcpd` are likewise
disabled rather than removed from the base image. Reclaiming that space would
require rebuilding the vendor rootfs or firmware image, which is outside the
clean repository boundary.

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
| /usr/bin/deneb-ui | ~1.8 MB stripped package binary | ELF MIPS (static, musl) native LVGL UI |
| /usr/bin/deneb-api | existing C API binary | Provides the local `digital-factory` command mode for UI-side status, connect, and disconnect actions over stock Gershwin coordinator IPC |

### Stock Menu Tree

`/home/cygnus/menu` is about 884 KiB in the recovery image. `executor.py` is
the stock touchscreen UI entry point. Deneb removes the dormant UI files after
the native UI smoke test passes, while retaining only the files required by
unreplaced stock services.

**Retained (remaining stock Python dependency):**

| File | Reason | Used By |
|------|--------|---------|
| `menu_settings.py` | Shared path, network, and endpoint constants | `coordinator/coordinator.py` (`GCODE_DIR`), `coordinator/handlers/filehandling.py` (`GCODE_FILE`), `coordinator/handlers/firmwareupdatehandling.py` (`FW_IMG_COPY_TARGET`, `FW_IMG_EXTRACT_DIR`), `coordinator/handlers/printhandling.py` (`GCODE_FILE`), `util/host_id.py` (`LAN_INTERFACE`), `util/network.py` (`LAN_INTERFACE`, `WLAN_INTERFACE`), `util/ufp_format.py` (`GCODE_DIR`) |
| `machine_config.json` | Machine geometry data (min/max X/Y/Z, type, material) | Referenced as path string by `menu_settings.MACHINE_CONFIG`; retained defensively for any stock service that reads it at runtime |

**Pruned (dormant UI implementation — removed after Deneb UI smoke passes):**

| File/Dir | Description |
|----------|-------------|
| `executor.py` | Stock touchscreen UI entry point |
| `controldialog.py` | Control dialog |
| `machine.py` | Machine configuration module |
| `pylvgl.py` | LVGL Python bindings (~46 KiB) |
| `screen.py` | Screen base class |
| `style.py` | UI style constants |
| `gui_companion/` | Path and progress bar companions |
| `helpers/` | Backlight and communication helpers |
| `img/` | ~40 splash icons and PNG assets |
| `navigator/` | ~30+ stock navigator files (print, materials, settings, maintenance) |
| `screens/` | ~40+ stock UI screen definitions |
| `templates/` | Screen template classes |
| `ui_elements/` | Button, label, bar, image, container widgets |

All `__pycache__` directories are also removed.

The prune boundary is verified at build time by
`deneb-stock-menu-prune-selftest` (host fixture) and the retained constant
dependency map is checked by `deneb-stock-menu-import-check`. See
[`tools/deneb-stock-menu-prune-selftest.sh`](../tools/deneb-stock-menu-prune-selftest.sh) and
[`tools/deneb-stock-menu-import-check.sh`](../tools/deneb-stock-menu-import-check.sh).

### SSH Access Helper

File: `tools/deneb-ssh.py`

Usage: `python tools/deneb-ssh.py '<command>'`

Uses paramiko with ssh-rsa monkey-patch to handle the ancient Dropbear.
Needs Python 3.13 with paramiko installed (`pip install paramiko`).

Alternative: `/c/Program Files/PuTTY/plink -batch -pw deneb root@10.10.10.244 '<command>'`

## Live Device Snapshot (2026-06-01, after `ec32a4a`)

### Running Processes and Memory

| Process | VSZ (kB) | RSS (kB) | Notes |
|---------|----------|----------|-------|
| python3 `coordinator.py` | 27,860 | 22,788 | Historical stock coordinator process; later Deneb source audit routes status/command clients directly to native `deneb-printsvc` |
| python3 `print_service.py` | 18,884 | 15,080 | Stock Marlin serial driver |
| deneb-ui | 2,780 | 1,472 | Deneb LVGL C UI |
| onion-helper | 1,968 | 1,196 | Generic Onion ubus helper; investigated, not disabled |
| netifd | 1,724 | 1,012 | Network interface daemon |
| rpcd | 1,788 | 956 | OpenWrt RPC daemon |
| ntpd | 1,220 | 900 | NTP client |
| odhcpd | 1,412 | 868 | AP-side IPv6 RA/DHCPv6 service; disabled by Deneb installer after this sample |
| logread mirror | 1,348 | 808 | Candidate if file mirror is optional |
| dropbear | 1,064 | 808 | SSH daemon |

### Memory State

```
Mem:              124584 total, 80440 used, 44144 free
-/+ buffers/cache:        41304 used, 83280 free
Swap:                  0 total,     0 used,     0 free
```

### Network Listeners

No HTTP server is listening. The live listeners are SSH plus localhost-only
stock backend sockets:

| Address | Service |
|---------|---------|
| 0.0.0.0:22 / :::22 | dropbear |
| 127.0.0.1:5546 | print_service Gershwin pub |
| 127.0.0.1:5548 | coordinator Gershwin pub |
| 127.0.0.1:5555 | print_service status PUB |
| 127.0.0.1:5556 | print_service command REP |
| 127.0.0.1:5565 | coordinator status PUB |
| 127.0.0.1:5566 | coordinator command REP |

### Print Service Investigation

`/home/cygnus/marlindriver/print_service.py` is the stock Marlin-facing service,
not a removable UI-side helper. It is started by `/etc/init.d/printserver` at
boot priority `S95`, before the coordinator at `S96`. The service opens
`/dev/ttyS1`, owns `/tmp/log/ultimaker/print.log`, and keeps localhost ZMQ
endpoints for status and command traffic:

- `127.0.0.1:5546`: Gershwin publish slot.
- `127.0.0.1:5555`: raw ZMQ status PUB, topic `10001`.
- `127.0.0.1:5556`: raw ZMQ command REP.

The service package is about 1,700 lines across `print_service.py`,
`marlin_datalink.py`, `marlin_protocol.py`, `marlin_executor.py`, serial-port
glue, and small Marlin companion helpers. `print_service.py` is the composition
root: it wires the serial driver, datalink framing, Marlin protocol parser,
G-code execution queue, and ZMQ/Gershwin sockets.

Observed responsibilities:

- Verify or program the motion-controller firmware at startup through
  `/home/atmel_programmer/prog.sh /home/atmel_programmer/cygnus-marlin.hex`.
- Stream jobs and macros to Marlin over `/dev/ttyS1`.
- Handle raw commands such as `JOB`, `MACRO`, `GCODE`, `ABORT`, `PAUSE`, and
  `RESUME`.
- Publish live printer state: bed/nozzle temperatures, top-cap state,
  coordinates, progress, requested state, idle/print status, errors, received
  Marlin faults, and firmware fields.
- Maintain flow control, CRC/framing, resend handling, pause/resume state, and
  macro file execution from `/home/cygnus/marlindriver/gcode/`.

The latest process sample was about 18.9 MB VSZ / 15.1 MB RSS, with about
7.5 MB anonymous/private RSS, 5 threads, and many local sockets. Replacing it is
one of the larger possible RAM wins, but it is also high risk: it sits directly
between Linux and the motion controller. Treat this as a clean-room replacement
project, not an installer disable candidate.

### Onion Helper Investigation

`onion-helper` is started by `/etc/init.d/onion-helper` at boot priority `S61`
from package `onion-helper 0.1-1`. The package owns only
`/usr/sbin/onion-helper` and its init script. The process registers a ubus object
named `onion-helper` with generic helper methods:

- `background`: spawn a command in the background.
- `echo`: echo a message.
- `write`: write data to a path, optionally base64-decoded or appended.
- `download`: fetch a URL to a path, optionally in the background.

No Deneb files, stock `/home`, `/etc`, `/usr/share`, `/www`, init, or hotplug
files referenced `onion-helper` during the 2026-06-01 scan. The broader Onion
board API is separate: the `onion` ubus object is provided by `onion-ubus`
through `/usr/libexec/rpcd/onion`.

A reversible live stop test left SSH, Ethernet client networking, `udhcpc`,
`deneb-ui`, `coordinator.py`, `print_service.py`, and the separate `onion` ubus
status call healthy. The daemon had about 1.2 MB RSS, but `/proc` showed only
about 100 KiB anonymous/private memory and about 1.1 MB file-backed library RSS.
Treat this as an attack-surface candidate with modest RAM benefit. Do not
disable it by default until more runtime workflows have been exercised.
