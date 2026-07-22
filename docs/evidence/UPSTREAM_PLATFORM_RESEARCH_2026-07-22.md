# Upstream Platform Research Snapshot

Verified: 2026-07-22

This file preserves the upstream-version and community research used to select
the initial OpenWrt and Marlin modernization baselines. It is dated evidence,
not a promise that these versions remain current. The active sequence and exit
gates live in
[PLATFORM_MODERNIZATION_ROADMAP.md](../PLATFORM_MODERNIZATION_ROADMAP.md).

## OpenWrt

The inspected stock image is OpenWrt \`18.06-SNAPSHOT r7499-44c7d0a524\` with
Linux \`4.14.81\`, musl \`1.1.19\`, BusyBox \`1.28.3\`, Dropbear \`2017.75\`,
OpenSSL \`1.0.2p\`, CA bundle \`20180409\`, GCC runtime \`7.3.0\`, and Python
\`3.6.15\`.

Official upstream release \`v25.12.5\` was published 2026-07-01 and remained
the latest OpenWrt release when rechecked on 2026-07-22. Its ramips target uses
Linux \`6.12\`, retains explicit Onion Omega2+ support, assigns the Omega2+ a
\`32448k\` firmware image limit, and includes USB, MMC, and U-Boot environment
packages.

This confirms that a current base is technically plausible. The generic
Omega2+ image is not printer firmware: Deneb still has to supply and validate
the printer-specific hardware description, storage/update layout, recovery
path, and userspace.

An in-place \`opkg upgrade\` from 18.06 is not a safe modernization route. The
kernel module ABI, libc/toolchain ABI, init/config behavior, package feeds,
firewall/network stack, and device-tree expectations have changed too much.
The roadmap therefore requires a complete reproducible replacement image.

## Marlin

UltiMaker's public \`Ultimaker/UltimakerMarlin\` repository has an
\`Ultimaker2+Connect\` branch at commit \`acb22046c69a\` dated 2021-02-19. That
branch targets the AVR Mega 2560, one extruder, 250000 baud, an E2 board,
ADS101X temperature inputs, custom power/board detection, no LCD, and no SD
card. The Linux SBC owns the UI and print file.

The branch documents a modified host serial protocol and points to a Griffin
protocol document that is not present in this repository. Deneb's source
comparison found 19 Connect-added M-code numbers, no added G-code numbers,
modified standard commands, and mandatory sequence/CRC/planner-window
transport. \`M290\`, \`M401\`, and \`M405\`-\`M407\` conflict with different
modern Marlin meanings. See
[MARLIN_COMMAND_PROTOCOL_AUDIT.md](MARLIN_COMMAND_PROTOCOL_AUDIT.md) for the
versioned command and slicer boundary.

Official Marlin release \`2.1.2.8\` was published 2026-06-24 and remained the
latest release when rechecked on 2026-07-22. Current configurations include
legacy Ultimaker 2 and Ultimaker 2+, but the reviewed upstream source did not
contain an E2 board, ADS101X support, or a 2+ Connect configuration.

Community evidence was thin when reviewed. A 2022 source-code question had no
useful implementation answer. A later community discussion reported deployed
2+ Connect machines still identifying as firmware 1.5.3 and said official
support was expected to end 2027-04-20, with further firmware updates unlikely
except for critical fixes. These community statements are context, not an
authoritative support commitment.

## Sources

- OpenWrt release: https://github.com/openwrt/openwrt/releases/tag/v25.12.5
- OpenWrt Omega2+ image definition: https://github.com/openwrt/openwrt/blob/v25.12.5/target/linux/ramips/image/mt76x8.mk
- OpenWrt Omega2+ device tree: https://github.com/openwrt/openwrt/blob/v25.12.5/target/linux/ramips/dts/mt7628an_onion_omega2.dtsi
- UltiMaker Marlin repository: https://github.com/Ultimaker/UltimakerMarlin
- UltiMaker 2+ Connect branch: https://github.com/Ultimaker/UltimakerMarlin/tree/Ultimaker2%2BConnect
- UltiMaker 2+ Connect branch commit: https://github.com/Ultimaker/UltimakerMarlin/commit/acb22046c69a50b8266d9cb047b0bdde217ec7a2
- Marlin release: https://github.com/MarlinFirmware/Marlin/releases/tag/2.1.2.8
- Marlin configurations: https://github.com/MarlinFirmware/Configurations
- Community source question: https://community.ultimaker.com/topic/42403-ultimaker-2-connect-where-is-the-firmware-sourcecode/
- Community LAN/support discussion: https://community.ultimaker.com/topic/47523-local-area-network-lan-printing-with-ultimaker-2-connect/
