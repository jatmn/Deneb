# Deneb

Deneb is a community firmware-mod project for the UltiMaker 2+ Connect. The goal is to make the printer more local-first, more responsive, and less resource-taxed while preserving safety-critical behavior and a path back to official UltiMaker firmware.

This repository is private while the project is being organized.

## Current Status

### Completed
- [x] SSH bootstrap package (enables SSH access for development)
- [x] Branding assets (splash screen, icons)
- [x] Early-boot framebuffer splash (S11, raw RGB565 to /dev/fb0)
- [x] Touchscreen UI replacement (LVGL v9 C, replaces Python/Cygnus menu)
- [x] Backend IPC integration (ZeroMQ client for coordinator)
- [x] Live device inspection (process list, memory, IPC ports, macros)
- [x] Baseline measurements documented
- [x] Initial on-device Deneb UI resource measurements

### In Progress
- [ ] Broader release testing of the new UI on hardware
- [ ] RAM/CPU benchmark comparisons while printing and updating

### Planned
- [ ] Web UI (lightweight local web interface)
- [ ] LAN printing (Cura compatibility)
- [ ] Generic slicer G-code support
- [ ] OS/service modernization
- [ ] Marlin firmware work

## Priorities

1. Keep the public/project repo legally clean.
2. Build and test the touchscreen UI replacement.
3. Keep resource reductions measurable as stock-menu parity expands.
4. Expand features only when the hardware behavior can be tested safely.

## Touchscreen UI

The stock Python/LVGL menu (33.7 MB VSZ in the stock baseline) has been replaced with a native LVGL v9 C implementation. The current measured Deneb UI process is about 2.7 MB VSZ / about 2 MB RSS at idle, with a roughly 8.6 MiB `.deneb` package after adding stock-menu coverage and generated i18n fonts. See [ui/README.md](ui/README.md) for build instructions and architecture details.

## Project Boundary

This repository must not contain UltiMaker firmware images, extracted root filesystems, proprietary application source, recovered/decompiled vendor code, private keys, device secrets, or generated modified firmware images.

Users/developers must supply their own firmware image or printer for local analysis. Deneb should publish original code, documentation, build scripts, installer scripts, compatibility layers, and minimal patch/transformation logic only where needed.

## Name

The stock firmware/application layer appears to use the codename `Cygnus`. Deneb is the brightest star in Cygnus. The name keeps a respectful relationship to that lineage while making clear this is a separate community mod, not official UltiMaker firmware.

## License

Original Deneb project files are intended to be licensed under MPL-2.0 unless a file or component says otherwise. This license does not apply to UltiMaker firmware, extracted firmware files, user-supplied device files, or third-party components with their own licenses.

See [LICENSE](LICENSE), [COMPLIANCE.md](COMPLIANCE.md), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Safety

This project can affect printer networking, motion, heating, updates, and diagnostics. Treat every change as hardware-affecting until proven otherwise. Do not expose unauthenticated heat, motion, print, or raw G-code controls on untrusted networks.
