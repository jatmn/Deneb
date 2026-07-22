# Deneb Platform Modernization Roadmap

Last updated: 2026-07-22

This is the active sequencing and acceptance-gate document. Current completion,
blockers, and defects belong in [PROJECT_STATUS.md](PROJECT_STATUS.md); dated
execution logs belong in [evidence/](evidence/README.md). Do not append
trial-by-trial completion history here.
This plan covers four related outcomes: remove Python completely, modernize the
Linux distribution and dependencies, produce a Deneb-owned firmware image, and
modernize the AVR Marlin controller firmware. It deliberately avoids in-place
major-version package upgrades on the production printer.

## Starting Point

The stock image is an OpenWrt 18.06 snapshot. The selected initial replacement
baseline is pinned OpenWrt `v25.12.5`, which supports the Onion Omega2+ base
hardware but is not a printer-specific image. Detailed versions, support
findings, and source links are preserved in
[the dated upstream research](evidence/UPSTREAM_PLATFORM_RESEARCH_2026-07-22.md)
and [the stock firmware audit](evidence/FIRMWARE_AUDIT.md).

Do not use `opkg upgrade` to bridge the stock image to the selected baseline.
The kernel, libc/toolchain, package, network, and device-tree boundaries require
a complete replacement image with an independently tested recovery path.

## Release Gates Used By Every Phase

- Reproducible inputs, pinned upstream commits/tags, checksums, and recorded
  toolchain/container version.
- No secrets or vendor images in Git; user-supplied stock artifacts stay local.
- A known-good full flash backup, factory partition backup, U-Boot environment,
  stock controller hex, and documented recovery procedure.
- Serial console access and a tested recovery route before the first flash.
- Functional matrix: boot, display/touch, Ethernet/Wi-Fi, USB, SD storage,
  printer UART/reset, idle, heat, home, print, pause/resume/abort, completion,
  update, rollback, diagnostics, Cura, and Digital Factory where enabled.
- Resource matrix: boot time, RSS/private memory, CPU, sockets, file descriptors,
  threads, log growth, tmpfs/overlay use, and flash/image size.
- One variable class per increment. Kernel/base, Deneb application, and Marlin
  changes are never first introduced in the same hardware test.

## Phase 0 - Truth And Recovery Baseline

1. Capture and periodically refresh `/proc/mtd`, U-Boot environment, kernel
   command line, loaded modules, device tree, GPIO mappings, UARTs,
   framebuffer/input identity, and the full package/process inventory.
2. Save full read-only dumps of U-Boot, environment, factory/calibration,
   firmware, and controller application/bootloader where legally and
   technically permitted.
3. Verify recovery through serial/U-Boot without overwriting production state.
4. Establish a spare-controller or spare-printer test target. Do not make the
   first base-image or Marlin experiment on the only recoverable machine.
5. Re-run the latest no-coordinator matrix and close or reopen every dated
   checklist item.

Exit gate: a written, exercised recovery runbook and a machine-readable hardware
inventory committed without vendor binaries or device secrets.

## Phase 1 - Finish Runtime De-Pythonization On The Legacy Base

Scope: close the remaining legacy-base workflow and recovery gates recorded in
[PROJECT_STATUS.md](PROJECT_STATUS.md), then remove the target Python runtime.

1. Prove the bounded one-command print stream stops/pauses physical motion in an
   acceptable interval; revise the protocol if the controller planner still
   defeats the bound.
2. Complete material load/unload/change state and physical motion.
3. Fix leveling Cancel to return to a known physical and UI state.
4. Expand material selection to the required stock-compatible brand/type/color
   depth and complete richer thumbnail, material, and nozzle preparation flows.
5. Prove diagnostics, update, reboot-after-complete, reboot-after-abort, USB,
   Cura, Web, and Digital Factory paths with the coordinator disabled.
6. Replace the Python AVR programmer:
   - implement Intel HEX parsing;
   - implement the known UART/STK500v2 and GPIO reset route;
   - implement or deliberately omit software-ISP recovery only after a recovery
     risk review;
   - verify MCU identification, flash, verify, failure interruption, bootloader
     restore, and application restore;
   - keep the stock Python tool available only in the laboratory until parity is
     proven.
7. Remove embedded target-side Python from the legacy bootstrap or freeze that
   installer as a legacy-only migration tool that runs before Python removal.

Exit gate: the full target workflow matrix and controller update/recovery run
without Python, and the only Python files in Git are desktop Cura/build tools.

## Phase 2 - Web/API Hardening And Productization

Architecture decision: retain lighttpd as Deneb's supported HTTP front end.
The stock firmware did not provide this Web UI; lighttpd was deliberately added
with it and is not legacy stock bloat. The boundary is strict: lighttpd owns
HTTP transport and edge concerns; `deneb-api` owns all printer and application
behavior. No printer state, safety policy, Cura semantics, or command execution
moves into lighttpd.

Increment A: instrument the current stack.

- Measure lighttpd, `deneb-api`, and `deneb-mdns` independently at idle, with
  one and multiple SSE clients, Cura polling, multipart upload, and print
  actions.
- Record RSS/private memory, CPU, descriptors, connection states, and recovery
  behavior rather than using VSZ alone.

Increment B: make lighttpd a deliberate edge service.

- Keep static-file GET/HEAD, index handling, and API reverse proxying in
  lighttpd instead of adding general-purpose HTTP serving to `deneb-api`.
- Add cache validators and immutable caching for versioned assets; keep API and
  live status responses no-store.
- Evaluate security headers, request/body limits, per-route timeouts, slow-client
  protection, bounded access logging, and a static maintenance/recovery page.
- Preserve streaming uploads and SSE without buffering entire request or
  response bodies.
- Evaluate TLS termination in lighttpd only if the selected TLS library,
  certificate lifecycle, CPU/RAM cost, and recovery behavior fit the target.
- Keep printer authorization, session decisions, validation, and control-state
  logic in `deneb-api`; do not move safety policy into Web-server configuration.

Increment C: harden the lighttpd/API boundary.

- Fix the observed four-SSE-client REST starvation and post-disconnect
  descriptor/socket retention.
- Establish one authoritative `deneb-api` service owner so restarting the Web
  stack cannot create duplicate API processes.
- Test malformed requests, interrupted uploads, slow readers/writers, reconnects,
  API restarts, maintenance mode, and browser/Cura concurrency.
- Require connection and descriptor counts to return to baseline without a
  reboot.
- After the current boundary is correct, prototype `mod_fastcgi` or `mod_scgi`
  with a reduced API transport adapter. Compare total binary size, module cost,
  RSS, CPU, upload/SSE behavior, disconnect cleanup, and recovery against the
  existing raw-HTTP Unix-socket proxy before choosing a migration.

Increment D: first-class UX.

- Implement the product gaps in `PROJECT_STATUS.md` with browser-level tests and
  hardware-backed workflow tests.
- Keep vanilla Web assets; do not introduce Node, Python, a database, or a
  client framework into the target image.

`deneb-mdns` remains a separate service by default. Merge it only if measurement
shows a meaningful saving and the reduced failure isolation is acceptable.

Exit gate: documented behavior coverage, no known safety-state lies, stable
connection cleanup, and a measured resource regression budget of zero unless
explicitly approved.

## Phase 3 - Reproducible Current OpenWrt Image

1. Pin OpenWrt `v25.12.5` initially. Do not develop against moving `main`.
2. Add a Deneb image-builder configuration/package feed with:
   - Omega2+ ramips/mt76x8 base;
   - Deneb UI, print service, API/web, optional Digital Factory, and tools;
   - no Python, pip, setuptools, compiler, package feed daemon, LuCI, database,
     or unused server packages;
   - only required USB/MMC/filesystem/network/crypto support.
3. Port the printer hardware description:
   - ILI9341 framebuffer/display path and SPI pin ownership;
   - FTS touch input and IRQ;
   - `/dev/ttyS1` controller link at 250000 baud;
   - AVR reset/software-ISP GPIO mapping;
   - backlight PWM and any frame-light/fan GPIO;
   - SD/MMC and persistent data/log partition mounting;
   - factory MAC/calibration and U-Boot environment preservation.
4. Cross-build every Deneb component against the new OpenWrt SDK. Prefer dynamic
   linking for libc and genuinely shared large libraries only after measuring
   total flash/RSS; keep leaf utilities static when it improves recovery.
5. Boot non-destructively first (U-Boot RAM/TFTP if supported, otherwise a
   dedicated recovery-safe test method). Validate hardware without heating or
   motion, then progress through the functional matrix.
6. Add a Deneb sysupgrade image and metadata only after the generic OpenWrt
   sysupgrade layout is proven compatible with the actual printer MTD map.
7. Define explicit data migration. Never preserve the full 18.06 overlay across
   the major upgrade; import only reviewed settings.

Exit gate: reproducible Python-free image, safe update/rollback, full target
matrix, and resource results no worse than the legacy Deneb base.

## Phase 4 - Independent Firmware Product

1. Replace stock-patching `.deneb` installation with Deneb-owned signed image
   and package/update metadata.
2. Implement A/B or recovery-partition strategy if the flash layout permits;
   otherwise require a tested U-Boot recovery image and atomic-enough update
   procedure.
3. Move mutable printer settings, print files, logs, and history to explicit
   data volumes with versioned migrations.
4. Remove all UltiMaker application files, Python runtime, dormant web assets,
   and proprietary Python packages from the generated rootfs.
5. Retain only interfaces/protocol compatibility needed for Cura, the controller,
   and optional Digital Factory, with clean-room provenance documented.
6. Add SBOM, license/source-offer artifacts, deterministic version identity,
   signed releases, rollback metadata, and factory/official restore instructions.

Exit gate: a clean checkout plus public upstream inputs can build the same Deneb
image without any UltiMaker firmware image or extracted rootfs.

## Phase 5 - Marlin Modernization

The command, response, transport, and slicer boundary is inventoried in
[the Marlin protocol audit](evidence/MARLIN_COMMAND_PROTOCOL_AUDIT.md).
Upstream versions, Connect hardware findings, and community context are
preserved in
[dated upstream research](evidence/UPSTREAM_PLATFORM_RESEARCH_2026-07-22.md).
This phase starts only after native AVR recovery exists and the Linux-side
workflow matrix is stable enough to distinguish controller regressions.

### Compatibility-layer boundary

Minimize the Marlin fork. Add a versioned preflight/translation layer to
`deneb-printsvc` for slicer validation, deterministic rewrites, legacy versus
modern serial transport, wait orchestration, and response normalization.
Prefer modern Marlin's native commands and `ADVANCED_OK` planner/buffer reports.

Do not move hardware authority to Linux. Thermal regulation, endstop and motion
limits, power budgeting, TMC2130 access, flow sensing, watchdogs, emergency
stops, and controller-attached fans/lights remain MCU responsibilities.
Provisioning-only private commands should become compile-time configuration or
bounded controller settings where practical. Any unavoidable Marlin additions
must live in one small Deneb compatibility module rather than changes scattered
through the planner, parser, and safety core.

### Recommended engineering approach

Do not begin by copying `Configuration.h` into Marlin 2.1. Treat this as a board
and protocol port:

1. Preserve and verify the stock `cygnus-marlin.hex` and bootloader recovery.
2. Build `acb22046c69a` reproducibly with a modern pinned AVR toolchain, compare
   the generated behavior/size, and run it in the existing simulator before any
   flash.
3. Freeze the Connect compatibility contract before writing the modern
   dispatch layer. Cover every command ID, case-sensitive parameter, response,
   stop reason, and removed host-owned command. Resolve number collisions
   explicitly; a compiling same-number handler is not compatibility proof.
4. Create a protocol conformance suite from Deneb `printsvc` traces: startup,
   M105/M114/M115, sequence/CRC framing, resend/reject, flow control, error
   strings, homing reports, temperature/topcap fields, power faults, and all
   custom G/M codes Deneb uses.
5. Publish versioned profiles and start/end templates for each supported slicer.
   Add representative generated-G-code fixtures and a preflight validator that
   classifies commands as pass-through, rewritten, Deneb-only,
   controller-private, unsupported, or unsafe/conflicting.
6. Inventory every E2-specific hardware behavior from the UltiMaker branch:
   pins, ADC/ADS101X, PWM, steppers/current, power FET sequencing, board ID,
   watchdog, thermals, endstops, fan/light behavior, bootloader and flash size.
7. Choose between:
   - forward-porting security/reliability fixes into the UltiMaker E2 branch
     first; or
   - adding an E2 board and compatibility protocol layer to Marlin 2.1.
   The first is the lower-risk first increment; the second is the strategic end
   state.
8. Prove a no-motion serial-only build, then cold sensors/endstops, fans/lights,
   one-axis low-speed motion, homing, controlled heat, and finally prints.
9. Require thermal runaway, sensor disconnect, stuck heater, endstop, watchdog,
   power-fail, serial corruption, pause, abort, and recovery tests before normal
   use.
10. Keep Marlin GPLv3 source/build artifacts in a clearly licensed component and
    publish corresponding source with distributed binaries.

Exit gate: protocol-compatible modern controller firmware passes the full
fault/print matrix and can be restored through the native AVR recovery tool.
