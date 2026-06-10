# Deneb UM2C Modding Checklist

Date: 2026-06-06

This checklist tracks what appears technically possible, what still needs proof, and how to keep public development exposure low. It is not legal advice. Treat it as an engineering and publication-risk checklist.

Current execution order:

1. Keep the legally clean project boundary intact while the repo grows.
2. Use the established SSH/bootstrap and `.deneb` package lanes for hardware validation.
3. Close release-critical touchscreen parity gaps: print preparation, material/profile depth, recovery/update states, and startup readiness.
4. Keep resource reduction measurable, especially around the remaining Python backend services.
5. Harden web/API controls, rollback/signing, local storage printing, Cura validation, and the native `marlindriver` replacement plan after the UI/status baseline is stable.

Current open focus:

- Hardware-proof the 20-screen touchscreen catalog against real idle, printing, paused, error, material, update, network, and recovery states.
- Finish `.deneb` rollback/signature verification before treating packages as stable releases.
- Measure print/update/upload/diagnostics resource behavior, not just idle UI memory.
- Validate Cura discovery, upload/start, pause/resume/abort, and pending-job visibility against current Cura builds on real hardware.
- Treat `marlindriver` / `print_service.py` as the next dedicated backend reduction target, while also auditing Deneb's current web/touch/API print-control shims for deduplication.

## 0. Project Identity

- [x] Working project/firmware mod name: `Deneb`.
- [x] Documented rationale:
  - The stock firmware/application layer appears to use the codename `Cygnus`.
  - Cygnus is both a mythological swan and a northern constellation, which fits a cloud-first printer identity that appears to lean into sky/constellation naming.
  - Deneb is the brightest star in the constellation Cygnus.
  - The name keeps a respectful relationship to the original `Cygnus` lineage while making clear this is a separate community mod, not official UltiMaker firmware.
  - Deneb also works thematically for this project: a local-first guide star for the UM2+ Connect, moving the printer away from cloud-only workflows while staying anchored to the original platform.
- [x] Add this naming rationale to the future public `README.md`.
- [x] Use wording that avoids implying UltiMaker endorsement, affiliation, or official status.

## 1. Confirmed Technical Facts

- [x] Firmware image is an OpenWrt/MIPS firmware for an Onion Omega2+-class platform.
- [x] Main application code is Python-heavy under `/home/cygnus`.
- [x] Touchscreen UI is launched by `/etc/init.d/menu` and runs `/usr/bin/python3 /home/cygnus/menu/executor.py`.
- [x] Touchscreen uses Linux-side framebuffer/input through LVGL bindings, not the motion-controller Marlin firmware.
- [x] Existing UI source is mostly Python and can technically be modified.
- [x] Stock UI code is not clearly licensed for public redistribution, so wholesale publication is risky.
- [x] Existing backend services expose print status, print handling, nozzle heating, bed heating, file handling, material handling, firmware update, and Digital Factory flows.
- [x] Existing print service supports raw-style command categories such as `GCODE`, `MACRO`, `JOB`, `ABORT`, `PAUSE`, and `RESUME`.
- [x] USB firmware update path accepts `.img` files from `/mnt/sda1`, extracts them to `/tmp/update`, and runs `/tmp/update/update.sh`.
- [x] Generic Onion USB `autorun.sh` is intentionally disabled in this image.
- [x] Cura LAN printing expectations should be taken from Cura `UM3NetworkPrinting` source and S-series API docs.
- [x] Extracted `/etc/passwd` contains `root` as the only interactive login account. No `ultimaker` Unix login user is present in this extracted image.
- [x] Extracted `/etc/config/dropbear` already has password auth and root password auth enabled on port 22.
- [x] Extracted `/etc/uci-defaults/97_disable_wifi_services` disables Dropbear unless `ultimaker.version.channel` is `internal`.

## 2. System-Wide Resource Reduction Budget

- [x] Project assumption: the stock firmware is already resource-taxed enough that maintaining current RAM/CPU behavior is not an acceptable long-term outcome.
- [x] Treat RAM, CPU, flash/storage, boot time, and UI latency reduction as release-blocking constraints for every Deneb feature.
- [ ] Establish baseline idle, booting, printing, paused, uploading, updating, and diagnostics-export resource usage on stock firmware before major replacements so we can prove measurable reductions.
- [x] Define initial reduction targets for each Deneb component: maximum resident memory, average CPU, peak CPU, startup time, disk footprint, log volume, socket count, and file descriptor count.
- [ ] Do not add a long-running service unless its memory and CPU budget is documented and measured on target hardware.
- [ ] New features must either reduce total system load, replace heavier stock behavior, or justify their cost with an explicit opt-in/disabled-by-default design.
- [ ] Prefer event-driven designs over polling loops; if polling is required, set explicit intervals and prove they do not affect print streaming or UI responsiveness.
- [ ] Prefer streaming/parsing in chunks over loading full print files, logs, thumbnails, or update metadata into memory.
- [x] Prefer simple native/static components for hot paths when they materially reduce RAM, CPU, startup time, or latency.
- [ ] Avoid adding heavyweight runtimes, package managers, databases, message buses, web frameworks, or frontend bundles unless there is no lighter practical option.
- [ ] Keep optional features disabled or lazy-loaded until the user opens them or enables them.
- [ ] Track memory fragmentation, leaks, process restarts, and swap/oom behavior during multi-day uptime and repeated print tests.
- [ ] Add resource reduction tests or hardware-in-the-loop checks for CPU, RAM, boot time, page transition latency, print-stream timing, and disk growth.
- [ ] Block Stable releases when core workflows do not show the expected resource reduction, unless the release is explicitly scoped as measurement-only or a temporary compatibility step.
- [ ] Treat "no worse than stock" as acceptable only for short-lived transition builds, not for the Deneb target state.
- [ ] Document resource tradeoffs in PRs and release notes when a feature adds a new service, dependency, background task, or persistent cache.

## 3. Legal, Licensing, And Publication Boundaries

### Public Repo Boundary

- [x] Keep the public project as an addon/mod kit, not a redistributed firmware fork.
- [x] Do not publish the extracted full firmware image, full `rootfs`, full `/home/cygnus`, full `/home/lib/stardustWebsocketProtocol`, full web assets, binaries, or modified complete firmware images.
- [x] Publish only original code we write, build scripts, installer scripts, documentation, and minimal patches where unavoidable.
- [x] Add `.gitignore` rules that exclude extracted firmware trees, downloaded images, generated modified images, private keys, logs, and device-specific identifiers.
- [x] Add a repo `README.md` explaining that users must supply their own printer/firmware and that the project does not distribute UltiMaker firmware files.
- [x] Add a `LEGAL_NOTES.md` or `COMPLIANCE.md` documenting the boundary between original addon code and vendor firmware artifacts.
- [ ] Review every copied snippet before commit. If it came from `/home/cygnus` or another UltiMaker-owned tree without a clear license, do not commit the full file.
- [ ] Keep patches small. Prefer installer transforms that insert or replace narrow sections over committing full modified vendor files.
- [ ] If a file is a tiny patch against UltiMaker-owned firmware code, mark it as a patch against user-supplied firmware and keep context minimal.
- [ ] Avoid publishing decompiled vendor code. Reverse-engineering notes, API maps, original compatibility implementations, and clean-room replacements are the safer direction.
- [x] Document warranty and safety risks clearly. This modifies printer behavior and may affect thermal, motion, and update safety.

### Repo License Decision

- [x] Recommended primary repo license for original addon code: Mozilla Public License 2.0 (`MPL-2.0`).
- [x] Reasoning: MPL-2.0 is file-level copyleft, so improvements to our original files stay open while still allowing the addon to run beside proprietary/unclearly licensed vendor firmware without trying to relicense that firmware.
- [x] Use `MPL-2.0` for the main addon repository unless a later legal review says otherwise.
- [x] Do not use a single repo-level license to imply rights over UltiMaker firmware files.
- [x] Do not use `MIT` as the default if we want downstream fixes to our addon files to remain open.
- [x] Do not use `GPL-3.0` as the default for the whole addon if the goal is to keep clean separation from proprietary/unclearly licensed firmware components.
- [x] Do not use `LGPL-3.0` as the default unless the repo primarily becomes a library intended for linking.
- [x] Add a `LICENSE` file containing MPL-2.0 for original project files.
- [x] Add SPDX headers to original source files, for example `SPDX-License-Identifier: MPL-2.0`.
- [x] Add a license scope note explaining that the license applies only to original files in this repo, not to UltiMaker firmware, extracted firmware, or user-supplied device files.
- [ ] Consider `Apache-2.0` only if we decide broad permissive reuse is more important than requiring changes to our files to stay open.
- [ ] Consider `AGPL-3.0` only if we intentionally want strong network-service copyleft for the web UI/API; this is probably too aggressive for compatibility with the firmware-adjacent addon model.
- [ ] Keep license choices per component if the repo grows:
  - Original installer, web UI, LAN API shim, touchscreen replacement: `MPL-2.0`.
  - Cura-derived compatibility code, if any: `LGPL-3.0` or compatible terms required by Cura.
  - Marlin fork/config/port: `GPL-3.0`.
  - Documentation: `CC-BY-4.0` or project default, to be decided.
- [ ] Add `REUSE`-style or SPDX-style license metadata if the repo starts mixing MPL/LGPL/GPL/docs licenses.
- [ ] Keep dependency choices compatible with `MPL-2.0` and the target firmware runtime.

### Notices And Disclosures

- [x] Add a `NOTICE` or `THIRD_PARTY_NOTICES.md` file for dependencies, firmware-interface notes, and attribution.
- [x] Track all new dependencies and licenses. Preserve notices for open-source components and update any about/legal disclosure shown in the UI/web UI.
- [ ] If any GPL/LGPL component is modified or redistributed, document source availability and license obligations.
- [ ] If any file is copied or adapted from Cura, keep that file/component under Cura's license, currently LGPL-3.0, and preserve attribution.
- [ ] If any Marlin firmware work is included, keep it in a clearly separate GPL-3.0-compatible tree or fork and comply with Marlin's GPL-3.0 obligations.
- [ ] Inventory the printer's own touchscreen/about/legal disclosure and mirror any relevant third-party notice obligations in our own disclosure.
- [ ] Verify whether the disclosure exists in this extracted image, in another firmware image, or is generated/shown from data not obvious in `/home/cygnus/menu`.
- [ ] Before publishing, compare our `THIRD_PARTY_NOTICES.md` against the stock touchscreen disclosure and the package/license inventory from the firmware image.

## 4. Delivery, Installation, And Rollback

### Priority 1: SSH Bootstrap Plan

- [x] Make the first implementation target a minimal SSH-only bootstrap update.
- [x] Do not bundle UI changes, web UI changes, LAN printing, service cleanup, diagnostics, or optimization work into the SSH bootstrap package.
- [x] Confirm target-device account state before building: check `/etc/passwd`, `/etc/shadow`, `/etc/config/dropbear`, `/etc/rc.d`, and `/etc/uci-defaults`.
- [x] Treat `root` as the confirmed default interactive login user unless live-device inspection proves another login user exists.
- [x] If a live device has an `ultimaker` Unix login account, include it in the SSH password setup; otherwise do not create extra users in the bootstrap package.
- [x] Set the bootstrap password to `deneb` for `root`.
- [x] If an existing `ultimaker` login user is found, set its bootstrap password to `deneb` too and verify it has an SSH-capable shell.
- [x] Ensure Dropbear password auth, root password auth, and root login are enabled.
- [x] Enable Dropbear at boot with `/etc/init.d/dropbear enable`.
- [ ] Start or restart Dropbear during install so SSH is available immediately after the update, not only after reboot.
- [x] Keep the package legally clean: only original `update.sh`, manifest, and documentation; no vendor files.
- [x] Build the bootstrap package as a tar-backed `.img` compatible with the existing touchscreen USB firmware update flow.
- [x] Test the package on target hardware by installing from USB, rebooting, and confirming SSH login on port 22.
- [x] Confirm both intended login paths:
  - [x] `root` with password `deneb`.
  - [x] `ultimaker` with password `deneb` only if that account exists on the live device.
- [x] Immediately after SSH access works, use the live device to gather resource usage, service state, storage layout, and hardware details instead of baking probe scripts into firmware packages.
- [x] Add clear security notes: bootstrap password is known, use trusted LAN only, and change the password after first login.
- [ ] Decide later whether Deneb should keep SSH enabled, make it toggleable, or disable it again after diagnostics are complete.

### USB Installer

- [x] Build USB `.deneb` installer packages that contain only our files and `update.sh` after the bootstrap update lane is installed.
- [x] Keep installer scripts small, deterministic, and low-memory; do not unpack or transform large firmware artifacts in RAM.
- [x] Release packages should be installable through the Deneb USB `.deneb` update flow and through our future in-device updater.
- [x] Installer should create a backup of touched files before changing anything.
- [ ] Installer should write a manifest of installed files, versions, hashes, and rollback instructions.
- [ ] Installer should provide a rollback mode.
- [ ] Installer should avoid modifying vendor files unless absolutely needed.
- [x] Add a version marker so the UI/web UI can display the mod version separately from UltiMaker firmware version.
- [ ] Test install, reboot persistence, uninstall, and failed-install recovery on a spare device before recommending use.

### Service Strategy

- [x] Treat limited compute, memory, and storage as first-order design constraints for every service decision.
- [ ] Assume current Python-heavy services may be contributing to latency, memory pressure, or print-quality issues until measured otherwise.
- [ ] Make CPU and memory reduction an explicit goal of every service audit, replacement, wrapper, or modernization decision.
- [x] Audit existing init services before deciding whether to add, wrap, modify, or replace them.
- [ ] Prefer the lowest-risk service strategy supported by evidence, not automatically a new service.
- [ ] If an existing service is poorly selected, fragile, insecure, overcomplicated, or part of a performance/reliability issue, consider replacing it with a cleaner service.
- [ ] For each service, decide whether to keep Python, optimize Python, split hot paths into native code, or replace the service with a more memory-light implementation.
- [ ] Prefer original replacement services in a memory-light language/runtime where they significantly reduce RAM, CPU, startup time, or latency.
- [ ] Avoid adding additional long-running Python processes unless their memory cost is measured and justified.
- [ ] Measure baseline RAM, CPU, open files, sockets, startup time, and steady-state behavior before and after service changes.
- [ ] Require before/after resource measurements for every service change that ships in Stable, with preference for changes that reduce total resident memory, idle wakeups, and boot cost.
- [ ] Keep service changes legally clean: publish original replacement services where possible, and keep modifications to vendor init files minimal or generated by installer logic.
- [ ] Document why each service is kept, wrapped, modified, disabled, or replaced.
- [x] If replacing the touchscreen UI, disable/wrap `/etc/init.d/menu` and install a new UI service rather than rewriting large sections of `/home/cygnus/menu`.
- [x] Keep the stock backend services running where possible: coordinator, printserver, firmware update, material handling, heating, and status channels.

### Official Firmware Escape Hatch

- [ ] Preserve a documented manual path for users who intentionally want to return to official UltiMaker firmware.
- [ ] Preserve a UI-accessible option to install an official UltiMaker firmware update intentionally, even after our mod is installed.
- [ ] Label official UltiMaker firmware install as a restore/override path and clearly warn that it may remove or disable our mod.
- [ ] Do not block official firmware installation when the user explicitly chooses it.
- [ ] Do not replace UltiMaker trust/signature verification for official firmware with our project signing key.
- [ ] Keep official-firmware restore separate from our Stable/Nightly mod update channels.
- [ ] If official firmware update checking is disabled by default, provide a manual "Install official firmware from USB" or "Restore official firmware" flow.
- [ ] Document how to download official firmware from UltiMaker and install it through the printer.

## 5. Updates, Signing, And Release Automation

### Update Channels

- [x] Disable or disconnect modified devices from UltiMaker firmware update checks so an official update does not overwrite or break the modded system unexpectedly.
- [ ] Replace UltiMaker update checks with an opt-in updater that checks our selected GitHub repository releases.
- [ ] Keep update checks lightweight: no always-on heavy updater process, no frequent polling, and no large release metadata retained in memory.
- [ ] Do not silently install updates. Updates should require user-visible confirmation from the touchscreen or web UI.
- [ ] Support two user-selectable update branches/channels: `Stable` and `Nightly`.
- [ ] Store the selected update channel persistently on the printer.
- [ ] Add touchscreen UI for selecting update channel: Stable or Nightly.
- [ ] Add web UI for selecting update channel if web UI settings are enabled.
- [ ] Stable channel should point at signed, tested GitHub Releases intended for normal users.
- [ ] Nightly channel should point at automated prereleases or release artifacts built from the main development branch.
- [ ] Make update checks compare installed mod version, selected channel, target release version, hardware compatibility, and minimum/maximum supported base firmware version.
- [ ] Make update UI clearly show release channel, version, date, changelog summary, and risk level before install.
- [ ] Document how to pin/disable updates entirely.
- [ ] Add update telemetry only if explicitly opt-in; default should be no external telemetry.

### Project Signing Model

- [x] We cannot sign updates as official UltiMaker firmware because we do not have, and should not attempt to obtain or impersonate, UltiMaker's private signing key.
- [x] We can sign Deneb mod/update releases with our own project signing key.
- [ ] Generate Deneb project signing keys outside the repo.
- [ ] Commit only the public verification key to the repo.
- [ ] Never commit private signing keys, private key passphrases, or raw secret material.
- [ ] Store the private signing key in GitHub Actions Secrets or another protected CI secret store.
- [ ] Store the private key passphrase, if any, as a separate GitHub Actions Secret.
- [ ] Configure GitHub Actions so signing only happens for approved release workflows, not arbitrary pull requests.
- [ ] Use protected environments or required approvals for Stable release signing.
- [ ] Allow Nightly signing only from trusted branches after automated validation.
- [ ] Ensure CI logs never print private key material, decoded secrets, or passphrases.
- [ ] Ensure release artifacts include a manifest, checksum, and signature.
- [ ] Verify artifact signature/hash on the printer before running update scripts.
- [ ] Keep signing keys out of generated artifacts except for public verification keys.
- [ ] Add a key-rotation plan and a recovery path if a key is compromised.
- [ ] Clearly document that Deneb signatures prove "released by Deneb project", not "approved by UltiMaker".

### Release Artifacts And CI

- [ ] Update package should include a manifest: project name, version, channel, commit, supported hardware, supported base firmware, installed files, checksums, and rollback metadata.
- [ ] Cache the downloaded update artifact locally and verify it before applying.
- [x] Build automation should generate Deneb update artifacts reproducibly.
- [ ] Build automation should fail if vendor firmware files, extracted rootfs files, private keys, or device-specific identifiers are accidentally included.
- [ ] Build automation should run lint/tests for installer scripts, service code, web UI code, and generated manifests.
- [ ] Build automation should run resource-reduction checks or enforce recorded hardware benchmark thresholds where practical.
- [ ] Build automation should create checksums and signatures for every release artifact.
- [ ] Build automation should publish GitHub Release notes from changelog entries.
- [ ] Nightly release process may publish prereleases automatically after validation.
- [ ] Stable release process should require manual approval and a release checklist.
- [ ] Add rollback package generation to the release process where feasible.

## 6. Core Firmware Workstreams

### Touchscreen UI Overhaul

- [x] Default direction: replace the Python-powered touchscreen frontend with an original, lighter implementation.
- [x] Do not preserve Python for touchscreen UI by default. Use Python only if profiling and implementation evidence show it is the best practical option.
- [x] Make lower memory use, lower CPU use, and faster screen transitions primary success criteria for the replacement UI.
- [x] Treat UI parity as incomplete until the replacement UI is demonstrably lighter and more responsive than the stock Python UI.
- [ ] Keep backend services only where they are useful, stable, and efficient enough after measurement.
- [x] Treat the existing `/home/cygnus/menu` UI as a behavioral reference, not as the implementation base.
- [x] Avoid patching stock Cygnus menu except for minimal installer handoff, service disablement/wrapping, compatibility shims, or emergency fallback.
- [x] Keep replacement UI source publishable by ensuring it is original code, not a copy of `/home/cygnus/menu`.

#### Replacement UI Architecture

- [x] Selected LVGL v9 C as the replacement UI runtime. Compiled native binary, direct framebuffer, minimal RAM.
- [x] Evaluated LVGL C, other native approaches. LVGL C chosen for best memory/latency/framebuffer fit.
- [x] Native compiled C component with clear memory/latency wins over Python.
- [x] No heavy frontend frameworks. Single LVGL binary, no web stack on touchscreen.
- [x] Single process (deneb-ui). No helper processes.
- [x] UI designed around 320x240 layout with flex/grid.
- [x] Minimal object trees, partial render buffer (40 lines = 25.6KB), no repeated image loads.
- [x] ZMQ SUB for status (non-blocking poll), page transitions never wait on backend.
- [ ] Add watchdog/logging around slow page transitions, render stalls, backend calls, and memory growth.
- [ ] If any Python remains in the UI, isolate it to non-hot paths and minimize persistent object trees, blocking calls, repeated image loads, and unnecessary allocations.

#### UI Test Automation / CI

- [ ] Build automated CI checks for touchscreen UI functionality.
- [x] Add a host-side UI simulator or test harness so navigation and state changes can be tested without a physical printer for every run.
- [ ] Add automated tests for every required menu route and screen transition.
- [ ] Add automated tests for button actions, back/close behavior, confirmation dialogs, and disabled/enabled states.
- [ ] Add automated tests for printer-state-driven UI changes: idle, preparing, printing, paused, aborting/canceling, complete, error/ER code, cooldown, USB missing, storage full, and network disconnected.
- [x] Add screenshot or framebuffer snapshot tests for critical screens where practical.
- [x] Add layout checks for 320x240 rendering: no overlapping text, clipped labels, unreadable buttons, or off-screen controls.
- [ ] Add locale CI checks using the JSON locale files.
- [ ] Add performance reduction checks for startup time, page transition latency, render loop stalls, and memory use.
- [ ] Add CPU reduction checks for idle UI, rapid navigation, print-status updates, temperature graph/refresh views, and modal-heavy flows.
- [ ] Add mocked backend services/status feeds so CI can simulate coordinator/printserver responses.
- [ ] Add hardware-in-the-loop smoke tests later for real touchscreen, input events, framebuffer rendering, and backend integration.
- [ ] Make UI CI run on pull requests before merge.
- [ ] Make release CI block Stable releases if required UI tests fail.

#### Baseline And Compatibility Mapping

- [x] Profiled stock: menu VSZ 33.7MB, all Python 113MB/124MB RAM. See docs/BASELINE_MEASUREMENTS.md.
- [x] Benchmark documented. Deneb binary 2.0MB (musl), estimated <0.3MB runtime RAM.
- [x] Map every current touchscreen screen, menu route, backend request, status dependency, and internal error/ER-code path.
- [ ] Use user-facing UltiMaker-style wording such as `Errors`, `Error codes`, or `ER codes`; avoid exposing internal implementation terminology unless the stock UI proves users already see it.
- [ ] Reuse existing UltiMaker ER codes and meanings wherever applicable.
- [ ] Build an ER-code mapping table from the firmware, UI, documentation, and any public UltiMaker references before adding new codes.
- [ ] Only introduce Deneb-specific error codes when no existing UltiMaker ER code accurately applies.
- [ ] Keep Deneb-specific codes clearly namespaced or distinguishable so they cannot be confused with official UltiMaker ER codes.
- [ ] Identify which current Cygnus UI behaviors must be preserved exactly, which can be improved, and which can be removed or hidden.
- [ ] Identify which backend services are UI dependencies versus implementation conveniences.
- [x] Build a compatibility test checklist for replacement UI parity.

#### Required Feature Parity

- [x] Preserve original feature structure unless a specific item is intentionally replaced or deferred:
  - [x] Print from USB. (screen_print: file browser + JOB command)
  - [x] Status screen. (screen_status: live temps/progress/position via ZMQ)
  - [x] Pause/resume/cancel. (screen_print: PAUSE/RESUME/ABORT buttons)
  - [x] Material workflows. (screen_material: load/unload via MACRO commands)
  - [x] Bed leveling/maintenance. (macro files mapped, maintenance UI present)
  - [x] Firmware update. (Deneb USB package flow present; official restore path still needs UX proof)
  - [x] Settings. (screen_settings: language selector)
  - [x] Errors / ER codes. (screen_error: ER code display)
  - [x] Cooldown warnings. (screen_temp: cooldown button)
  - [x] About/legal. (screen_about: version, license, credits)

#### New Touchscreen Functionality

- [x] Add first-class touchscreen controls for manual motion jogging. (screen_jog)
- [x] Support X/Y/Z axis jogging from touchscreen with safe step sizes (1/10/50mm) and feedrates (F3000).
- [ ] Decide whether extruder jogging is included, and if so require safe nozzle temperature before extrusion.
- [x] Add first-class touchscreen controls for manual nozzle temperature setting. (screen_temp: slider + M104)
- [x] Add first-class touchscreen controls for manual bed temperature setting. (screen_temp: slider + M140)
- [x] Add touchscreen cooldown controls for nozzle, bed, and combined cooldown. (screen_temp: cooldown button)
- [x] Show current and target temperatures clearly on manual temperature screens. (screen_temp: live update via lv_timer)
- [x] Show heating/cooling progress where useful.
- [x] Add guardrails so manual motion, heating, and cooldown actions are blocked or limited during incompatible printer states.
- [ ] Reuse existing backend heating/cooldown/motion primitives where safe; replace or extend them where stock support is too limited.
- [ ] Add clear confirmation/warning behavior for actions that can move hardware or heat components.
- [ ] Ensure manual touchscreen motion and temperature actions are visible in diagnostics, preferring existing firmware logs when they already capture the needed event.
- [ ] Add Deneb-specific logs for manual touchscreen actions only where stock firmware logs are missing, ambiguous, or insufficient for troubleshooting.

#### Internationalization

- [x] Keep all visible strings in standard language/resource files from the beginning. (JSON locale files)
- [x] Use JSON locale files for touchscreen and web UI text.
- [x] Support default locale set:
  - [x] English (`en`) as default/source/fallback.
  - [x] Dutch (`nl`).
  - [x] French (`fr`).
  - [x] German (`de`).
  - [x] Chinese Simplified (`zh-Hans`).
  - [x] Talk like a Pirate (`en-pirate`).
  - [x] 1337 $P34k / Leet Speak (`en-1337`).
- [ ] Define a locale file schema, including message keys, default English strings, interpolation rules, comments/translator notes strategy, and fallback behavior.
- [ ] Use stable BCP 47-style locale identifiers where practical.
- [ ] Treat novelty locales as first-class supported locales for testing layout, fallback behavior, and translator tooling.
- [ ] Ensure Chinese Simplified font/rendering support is available on the target UI stack before claiming support complete.
- [ ] Add CI validation for JSON locale syntax, missing keys, unused keys, duplicate keys, and placeholder mismatches.
- [ ] Keep locale files easy to edit by non-developers.
- [ ] Verify whether the stock touchscreen supports more than one language.
- [ ] Build language fallback behavior and missing-translation diagnostics.

#### About, Legal, And Notices

- [x] Add or update about/legal disclosure for changed dependencies, removed dependencies, and original project notices.

### Lightweight Web UI

- [x] Build a lightweight local web UI, preferably served by our own small service.
- [x] Avoid a heavy SPA. Use simple static HTML/CSS/JS or a very small framework only if justified.
- [x] Set explicit web UI budgets for RAM, CPU while idle, CPU while polling status, bundle size, and request latency.
- [x] Keep the web UI optional and low-impact so enabling it does not undo touchscreen/service memory reductions.
- [x] Prefer static assets and small JSON endpoints over server-side rendering, heavy websocket fanout, or large client frameworks.
- [x] Expose printer status, current file, progress, time remaining, printer errors / ER codes, bed/nozzle temperatures, and firmware/mod version.
- [x] Display existing UltiMaker ER codes and recommended actions where applicable.
- [x] Add controls for start, pause, resume, stop/cancel.
- [x] Add manual nozzle heat and bed heat controls with safe limits.
- [x] Add cooldown controls.
- [x] Add manual jogging for X/Y/Z with safe feedrates and clear lockouts.
- [ ] Decide whether web UI extruder jogging is included, and if so require safe nozzle temperature before extrusion.
- [x] Add guardrails so jogging/heating is blocked or limited during active print states unless explicitly safe.
- [ ] Add file upload/list/start for local storage if storage behavior is confirmed safe.
- [x] Keep all web UI strings in the same translation/resource strategy as touchscreen UI if practical.
- [x] Include license/about page listing our project license and third-party dependencies.

### USB And Local Storage Printing

- [ ] Verify the current USB print flow end to end.
- [ ] Confirm whether `.ufp` and `.gcode` are copied/extracted to local storage before printing.
- [ ] Confirm whether USB can be safely removed after a print starts when an SD card/local storage is installed and detected.
- [ ] If not reliable, change flow so print jobs are copied to local storage first, verified, then printed from local storage.
- [ ] Copy and verify files in streaming chunks so large jobs do not cause memory spikes.
- [ ] Add user-visible copy/progress/error states.
- [ ] Add free-space checks before copying.
- [ ] Add hash or size verification after copy.
- [ ] Avoid relying on USB during print once local storage is available.
- [ ] Document behavior when local storage is missing, full, corrupt, or mounted read-only.

### LAN Printing For Cura

- [x] Pull or inspect current UltiMaker Cura source, especially `UM3NetworkPrinting`, as source of truth.
- [x] Inspect S-series/local cluster API docs and compare with Cura client behavior.
- [x] Produce a compatibility matrix of required Cura endpoints, fields, request formats, and response formats. (Captured in docs/WEB_UI.md and docs/CURA_INTEGRATION.md; keep expanding with live Cura evidence.)
- [x] Implement mDNS/zeroconf advertisement so Cura's local network scanner can discover the printer. (`deneb-mdns`)
- [x] Add Deneb Cura network discovery plugin so `deneb_um2c` maps to Cura's stock `ultimaker2_plus_connect` profile.
- [ ] Validate the advertised `machine` TXT value and Cura plugin against current Cura builds so UM2+ Connect is recognized without selecting the wrong printer profile.
- [x] Implement enough `/cluster-api/v1/` to support detection and status first.
- [x] Keep LAN API implementation lightweight and mostly idle when no Cura client is connected.
- [ ] Prefer implementing LAN printing by replacing or reusing existing communication paths instead of adding a heavy always-on stack.
- [x] Avoid buffering full uploads in memory; stream uploads to local storage.
- [ ] Add complete free-space and upload failure-mode checks.
- [x] Implement receiving print jobs from Cura after detection/status is stable.
- [x] Map uploaded Cura jobs into the existing firmware print flow instead of bypassing validation when possible, using pending-job metadata during validation/conflict/preheat.
- [x] Support pause, resume, abort/cancel, job status, progress, time remaining, temperatures, and printer errors / ER codes.
- [ ] Validate Cura upload/start, pause/resume, abort/cancel, and pending-job status on real hardware.
- [x] Move the temporary Python/Gershwin conflict/preheat bridges onto native Deneb helpers and backend commands so Cura/touchscreen pending-job actions no longer depend on embedded Python launchers.
- [ ] Preserve existing UltiMaker ER code values in LAN API responses where applicable.
- [ ] Investigate queue behavior separately. Stock backend appears single-active-job, so any queue is likely our layer.
- [ ] Research whether camera endpoints are relevant. Do not assume camera support exists on this hardware until firmware and hardware evidence confirm it.
- [ ] If camera is possible, identify actual camera hardware, device nodes, drivers, bandwidth, and S-series API expectations before implementing.
- [ ] Add LAN printing security: disabled by default or paired, local-network only by default, and no unauthenticated destructive controls exposed to untrusted networks.

### Generic G-code And Slicer Compatibility

- [ ] Research current file validation source before changing behavior. No hand waving.
- [ ] Identify current special header requirements for basic `.gcode` files.
- [ ] Determine exactly which header fields Cura-generated G-code provides and why the firmware requires them.
- [ ] Add detection for standard Marlin 2.0-style G-code from PrusaSlicer, OrcaSlicer, and other slicers.
- [ ] Support standard G-code without requiring UltiMaker/Cura-specific headers when safe.
- [ ] Determine how to infer or ask for missing metadata such as material, nozzle size, bed temperature, nozzle temperature, estimated time, and thumbnail.
- [ ] Support embedded thumbnails used by PrusaSlicer/OrcaSlicer where present.
- [ ] Research thumbnail encodings and conventions used by target slicers.
- [ ] Parse G-code metadata and thumbnails in a streaming or bounded-memory way.
- [ ] Preserve support for `.ufp` and Cura-generated files.
- [ ] Add validation modes: strict UltiMaker metadata, standard Marlin metadata, and fallback/manual metadata.
- [ ] Ensure unsupported or unsafe G-code fails clearly before heating or moving.
- [ ] Add test files from Cura, PrusaSlicer, OrcaSlicer, and plain Marlin samples.

## 7. Reliability, Security, And Modernization

### Boot Time And Readiness

- [ ] Investigate and fix slow boot-to-ready time.
- [ ] Treat boot CPU spikes, memory spikes, and Python import cost as first-class boot blockers.
- [ ] Define success as reducing boot-to-ready time and peak boot resource pressure compared with stock firmware, not only explaining why boot is slow.
- [ ] Define what `ready` means: touchscreen responsive, backend services running, printer status available, storage mounted, network optional/available, heaters/motion safe, and no blocking update/recovery tasks.
- [ ] Measure boot timeline from power-on to ready status.
- [ ] Instrument boot stages: bootloader/kernel, OpenWrt init, storage mounts, network, coordinator, printserver, touchscreen UI, Digital Factory/cloud services, update checks, material/profile loading, and any added Deneb services.
- [ ] Identify services that block readiness unnecessarily.
- [ ] Move nonessential work after ready state where safe.
- [ ] Parallelize independent startup tasks where safe.
- [ ] Avoid parallelizing startup tasks when it causes CPU/RAM contention that delays actual ready state.
- [ ] Defer cloud/update/network checks so local printing and UI readiness are not blocked by external services.
- [ ] Check for slow sleeps, retries, network timeouts, filesystem scans, QR generation, material/profile loading, and Python import/startup costs.
- [ ] Add boot-time logging that survives reboot using the existing persistent log location where practical.
- [ ] Integrate Deneb logs into the existing touchscreen "Export log files" support flow instead of inventing a separate diagnostics export.
- [ ] Rename Deneb-generated support export archives so the filename is clearly prepended with `Deneb`, avoiding confusion with official UltiMaker support bundles.
- [ ] Add a root-level `README` or `NOTICE` file inside the exported support archive explaining that the printer is running the Deneb community mod, this is not an official UltiMaker firmware state, and Deneb support should be handled through the Deneb GitHub project.
- [ ] Include Deneb GitHub/support URL in the support-export `README`/`NOTICE`.
- [ ] Make the notice polite and clear for UltiMaker support so users are redirected appropriately instead of confusing official support channels.
- [ ] Add a Deneb-specific subfolder inside the exported support archive for Deneb logs, manifests, boot timing, update history, and service diagnostics where applicable.
- [ ] Reuse or mirror the firmware's existing log rotation behavior where practical. The current Python logging helper uses timed daily rotation and `backupCount=7` under `/var/log/ultimaker`.
- [ ] Keep Deneb diagnostic exports free of private keys, signing secrets, network passwords, access tokens, and device-unique sensitive data unless explicitly required and redacted.
- [ ] Add a user-visible startup/loading screen with progress or staged status messages.
- [ ] Make startup progress honest: show real boot stages rather than a fake timer.
- [ ] Provide clear error state if boot reaches degraded-ready mode, such as network unavailable, local storage missing, or update check skipped.
- [ ] Ensure the replacement touchscreen UI can start early enough to show boot progress.
- [ ] Keep boot progress lightweight and safe even if backend services are not ready yet.
- [ ] Add CI or integration checks for boot-order assumptions where practical.
- [ ] Add hardware-in-the-loop timing tests later to catch boot-time regressions.

### Logging And Diagnostics Strategy

- [ ] Inventory existing UltiMaker log sources before adding new Deneb logging.
- [ ] Map which stock logs already cover boot, service startup, touchscreen actions, print flow, heating, motion, storage, networking, update checks, Digital Factory, and ER/error states.
- [ ] Prefer existing firmware logs wherever they already capture the event with enough timestamp, context, and severity detail.
- [ ] Avoid duplicating stock log lines in Deneb logs unless duplication is needed to correlate across a new Deneb component boundary.
- [ ] Keep Deneb logs scoped to original Deneb services, replacement UI behavior, update decisions, compatibility layers, and gaps not already covered by stock firmware logging.
- [ ] When Deneb calls an existing firmware backend, log only Deneb-side intent/correlation IDs/outcomes needed to connect the action to stock backend logs.
- [ ] Define a shared diagnostic event schema for Deneb logs: timestamp, component, event, severity, printer state, correlation ID where useful, and redaction status.
- [ ] Keep logging volume low on hot paths so diagnostics do not worsen UI latency, print streaming, flash wear, or memory pressure.
- [ ] Avoid expensive log formatting, synchronous flushes, or high-frequency diagnostic sampling on print-stream and UI hot paths.
- [ ] Include stock logs and Deneb logs together in support exports, with Deneb logs isolated in their own subfolder and a manifest explaining what each log source covers.
- [ ] Document which logs should be checked first for common problems so troubleshooting starts with the most authoritative stock source when applicable.

### Print Quality Degradation Investigation

- [ ] Reproduce the reported issue: after uptime over one day, or after reprinting the same job more than twice, print quality degrades as if pauses are induced.
- [ ] Define a repeatable test print, firmware version, material, temperature, USB/local file source, and environmental conditions.
- [ ] Capture printserver/coordinator logs before, during, and after the degradation.
- [ ] Monitor memory, swap, CPU load, file descriptor count, and process restarts over a multi-day run.
- [ ] Track Marlin communication timing, resend/ack behavior, buffer starvation, and any pauses inserted by the Python print pipeline.
- [ ] Compare repeated prints from USB path versus copied local `/home/3D/model.gcode` path.
- [ ] Check whether stale state in printserver, coordinator, G-code stream queues, or touchscreen UI state survives across repeated prints.
- [ ] Add diagnostics to detect planner starvation, serial backpressure, G-code stream stalls, and long Python GC pauses.
- [ ] Fix root cause before adding major new features if the bug affects reliability.

### OS And Service Modernization

- [ ] Inventory every enabled init service, process, socket, cron job, hotplug script, and network listener.
- [ ] Classify each service as stock OpenWrt/Onion, UltiMaker-modified, third-party package, or unknown/custom.
- [ ] Identify which services are required for printing, touchscreen, networking, USB, firmware update, storage, time, logs, and recovery.
- [ ] Identify services that are obsolete, unused, redundant, unsafe, or only needed for factory/cloud workflows.
- [ ] Check OpenWrt-specific packages, kernel, and base networking stack against known OpenWrt security history.
- [ ] Do not spend time on detailed CVE triage for every bundled non-OpenWrt/Python dependency unless it remains exposed or cannot be upgraded/replaced directly.
- [ ] For unmodified standard packages with a straightforward upgrade path, prefer updating to modern versions and fixing compatibility issues that arise.
- [ ] Prefer modernization choices that reduce resident service count, idle CPU wakeups, RAM use, and boot-time work.
- [ ] Prefer removing, replacing, or disabling unnecessary services over simply updating them if that gives a larger CPU/RAM win without breaking required printer behavior.
- [ ] Prioritize exposed OpenWrt/base components first:
  - [ ] kernel/base-files/procd/ubus/rpcd
  - [ ] firewall/iptables/netifd
  - [ ] dropbear
  - [ ] dnsmasq
  - [ ] odhcpd
  - [ ] nodogsplash/libmicrohttpd, if retained
  - [ ] curl/OpenSSL/mbedTLS/CA certificates
  - [ ] any mDNS/HTTP service we add
- [ ] Determine whether the firmware uses OpenWrt 18.06-era packages unchanged or modified locally.
- [ ] For standard/unmodified packages, update/rebuild from known upstream OpenWrt/Onion sources when there is a clean path.
- [ ] For modified packages or scripts, diff against upstream before updating so vendor-specific hardware behavior is not lost.
- [ ] Preserve required UltiMaker hardware integrations while removing unnecessary cloud/factory assumptions only when safe.
- [ ] Build a service dependency map before disabling anything.
- [ ] Add boot-time and runtime smoke tests after any OS/service update.
- [ ] Add rollback for OS/service changes because a broken network, storage, or UI service can strand the device.
- [ ] Track free flash/RAM before and after updates; security upgrades cannot make the device unstable.
- [ ] If full OS modernization becomes too risky, isolate high-risk network services behind local firewall rules and update only the exposed pieces first.

### Safety And Security

- [ ] Add hard temperature limits matching or improving current firmware constraints.
- [ ] Require explicit user confirmation for dangerous operations.
- [ ] Disable motion jogging while printing unless a specific safe maintenance mode allows it.
- [ ] Block extrusion below safe nozzle temperature.
- [ ] Avoid exposing raw arbitrary G-code in web UI by default.
- [ ] If raw G-code is added, hide behind an explicit advanced/developer mode with warnings.
- [ ] Add authentication or pairing for web UI and LAN controls.
- [ ] Bind services carefully and document network exposure.
- [ ] Ensure remote start/pause/resume/cancel/heat/jog actions are auditable, using existing firmware logs first where they already capture the action clearly.
- [ ] Add Deneb-specific remote-control logs only for Deneb web UI/API/LAN layers, correlation to backend calls, authorization decisions, and gaps not covered by stock logs.
- [ ] Provide a physical-touchscreen-visible indication when remote control is active.

## 8. De-Python marlindriver / Native Print Service

Resource rationale and measurement guardrails live in
[docs/RESOURCE_REDUCTION_PLAN.md](docs/RESOURCE_REDUCTION_PLAN.md#next-measurement-targets);
this section is the milestone checklist for that same work. Current proof and
remaining promotion gates live in
[docs/PRINTSVC_EVIDENCE_LEDGER.md](docs/PRINTSVC_EVIDENCE_LEDGER.md). Stock
Python source comparison and no-overclaim rules live in
[docs/PRINTSVC_LEGACY_PARITY_AUDIT.md](docs/PRINTSVC_LEGACY_PARITY_AUDIT.md).

Current native replacement progress lives under [printsvc/](printsvc/). The
current buildable slice provides a native-default experimental
`deneb-printsvc` binary with separate modules for ZMQ IPC, IPC command-frame
handling, command parsing, status serialization, Marlin status parsing, serial
transport, packet/CRC helpers, flow control, G-code streaming, heater waits,
macro lookup, diagnostics, job lifecycle, and service state. It is packaged
into `.deneb` releases, Deneb clients route directly to native
`deneb-printsvc`, and the generated printserver handoff no longer delegates
back to the stock driver through a Deneb config flag.

Evidence rule for this section: a checked implementation, parser, harness, or
packaging item is not live hardware parity unless the item explicitly says it
was proven on hardware. Stock Python source comparison is required for behavior
claims, and live hardware/client proof remains open until captured with the
native service running. Static package gates and host tests may prove that a
guardrail exists, but they do not prove that the physical printer behaved
safely or that every client path is migrated.

Legacy Python source anchors checked during the Section 8 honesty audit:
`print_service.py` binds status PUB `127.0.0.1:5555`, command REP
`127.0.0.1:5556`, publishes topic `10001`, and defaults `firmware` from
`marlin-version` to `none`; `marlin_protocol.py`'s service loop schedules
`M105` and `M114`, not `M115`; `marlin_companion/protocol.py` can parse
`MACHINE_TYPE`, `PCB_ID`, and quoted `BUILD` version output when such a line is
observed; `marlin_executor.py` rewrites `M190`/`M109`, ignores/replaces `G280`,
and aborts with relative XY wipe, Z move, `G28 X Y`, then `G28 Z`. Native
behavior that deliberately improves safety must be documented as a Deneb
deviation, not hidden under "stock parity."

- [x] Treat `marlindriver` replacement as a dedicated milestone, not an opportunistic bug fix.
- [x] Build an experimental original native `deneb-printsvc` replacement track
  for `/home/cygnus/marlindriver/print_service.py`.
- [x] Preserve the stock print-service IPC contract first: raw ZMQ status PUB on `127.0.0.1:5555`, command REP on `127.0.0.1:5556`, topic `10001`, and `COMMAND<json>` request framing.
- [ ] Prove coordinator, LCD UI, web UI, Cura LAN flow, and Digital Factory
  behavior against the native service on hardware. This is not complete until
  those clients work through native `deneb-printsvc` without silently falling
  back to stock Python or carrying stale print state. Current observe-only
  client evidence covers native route ownership, UM API status/root/system,
  Cura cluster printers/print_jobs/materials, and Digital Factory bridge status
  on hardware. A later June 9, 2026 `34518e8` supervised Cura cluster
  upload/start/abort smoke used a generated bounded Z-only fixture with no
  heat, no extrusion, and no X/Y moves; it proved native route ownership, active
  `printing` with Stop allowed, accepted cluster abort, native `aborting` with
  Stop disabled while cleanup drained, and final `idle` with Stop disabled.
  This still does not prove LCD UI interaction, Web UI workflows,
  representative Cura geometry/client behavior, or Digital Factory job
  lifecycle behavior.
- [x] Finish the Deneb integration audit for code that patched around the stock
  Python driver: LCD `backend_comm`, web `backend_zmq`, `api_print_job`,
  `api_cluster`, `api_printer`, conflict/preheat bridges, pending-job metadata
  files, direct macro calls, direct raw G-code calls, and duplicated status
  classification. Record each remaining shim's owner and removal condition.
  The tracked artifact is
  [docs/PRINTSVC_INTEGRATION_AUDIT.md](docs/PRINTSVC_INTEGRATION_AUDIT.md),
  and `deneb-printsvc-integration-audit` plus its negative-fixture selftest now
  gate source, package, archive, installer, and CTest paths.
- [x] Finish deciding, per integration, whether the behavior remains a client of
  native `deneb-printsvc`, moves into `deneb-printsvc`, or belongs in a shared
  Deneb print-control library/API. The placement-decision column in
  [docs/PRINTSVC_INTEGRATION_AUDIT.md](docs/PRINTSVC_INTEGRATION_AUDIT.md)
  records this for each patched client boundary and is enforced by
  `deneb-printsvc-integration-audit`.
- [ ] Avoid preserving patchwork solely for compatibility with the stock
  driver's shape; compatibility shims are acceptable only as temporary migration
  boundaries with explicit removal criteria. The integration audit now records
  each boundary and removal condition, but this remains open until the listed
  hardware client flows prove the boundaries are thin adapters rather than
  hidden compatibility behavior.
- [x] Create shared Deneb print-control helpers for status classification,
  pending-job metadata, command formatting, macro lookup, safe motion policy,
  heat-state decisions, pause/resume/abort semantics, and error mapping.
- [ ] Finish migrating LCD UI, web UI, REST API, Cura cluster API, diagnostics,
  and native `deneb-printsvc` callers onto those shared helpers so they no
  longer reinterpret print state, pending jobs, abort state, preheat state, and
  macro safety differently. The integration audit now enforces the helper
  dependencies and key context/status helper calls for LCD `backend_comm`, web
  `backend_zmq`, REST print-job/printer APIs, and Cura cluster routes, but this
  remains open until the remaining adapter-specific lifecycle code is removed
  or proven to be display/transport-only on hardware.
- [x] Add tests around shared print-control helpers before removing duplicate
  callers so web/touch/API behavior stays aligned during migration.
- [x] Keep the native driver source tree intentionally modular; do not create a few thousand-line catch-all files.
- [x] Start with named source units for clear responsibilities, for example: service main/init, ZMQ IPC, print-control API, serial transport, Marlin packet framing, CRC, status parsing, command parsing, G-code stream reader, macro registry, job queue, heater waits, pause/resume state, abort/finalize motion policy, diagnostics/logging, and test fakes.
- [x] Require new `deneb-printsvc` files to have narrow public headers and focused tests where practical, especially for packet framing, status parsing, G-code streaming, and state-machine behavior.
- [x] Port the Marlin serial packet layer cleanly: `/dev/ttyS1`, 250000 baud, CRC16 packet generation, CRC8 sync parsing, sequence numbers, ACK/reject handling, resend queues, and flow-control limits.
- [x] Port host-tested parser support for M105 hotend/bed/topcap telemetry, M114
  position/R0, G28 home-distance telemetry, recoverable Marlin log messages,
  Marlin fault messages, and M115/version-output recognition. Native
  temperature parsing accepts both plain `T:` and older indexed `T0:` nozzle
  reports, old-Marlin compact bed reports such as `B25.1/0.0@0`, and topcap
  reports such as `t1/33.2`, so idle LCD/API temperatures do not fall back to
  impossible `0/0C` values when the firmware uses stock compact telemetry. This
  does not by itself prove the native runtime is requesting every status source.
- [x] Add bounded native firmware/version probing as a Deneb diagnostic
  extension, not a stock-parity claim. Stock Python parses `MACHINE_TYPE`,
  `PCB_ID`, and quoted `BUILD` lines when they are observed and publishes
  `firmware` from `marlin-version`, defaulting to `none`; local source review
  does not show stock Python scheduling `M115` in its normal status loop.
  Native now sends `M115` with the startup `M105`/`M114` probe and retries
  `M115` a bounded number of times while firmware metadata remains absent,
  while keeping recurring status polling on the stock-shaped `M105`/`M114`
  cadence. Host tests prove startup `M115`/`M105`/`M114`, bounded retry, and
  periodic polling without duplicate `M115`.
- [ ] Verify firmware/version status behavior live against stock and native.
  Accepted evidence must compare stock Python and native `deneb-printsvc` on
  the same old-Marlin firmware: `firmware:"none"` is acceptable only if stock
  also reports `none` under the same conditions; native must preserve the field
  and default, must parse non-`none` metadata if the controller emits version
  output, and must not regress temperature/topcap/status updates while stock
  Python is absent. The smoke harness now has an observe-only
  `--firmware-proof` mode plus verifier/comparer checks for firmware,
  machine/PCB metadata, nonzero ambient bed/nozzle temperatures, topcap
  telemetry, and scalar status; this is tooling only until a live stock/native
  pair is captured on the same printer. On June 9, 2026,
  `dist/Deneb_Update_34518e8.deneb` was installed and an observe-only native
  run passed `/usr/bin/deneb-printsvc-smoke-verify --native --idle
  --boot-sync --client-proof --firmware-proof
  /tmp/deneb-printsvc-firmware-proof.summary`; it proved native route
  ownership, no stock `print_service.py`, `firmware=none`,
  `machine_type=none`, `pcb_id_valid=false`, live ambient bed/nozzle
  temperatures around 30.1 C / 33.2 C, topcap present/current around 30.0 C,
  and scalar `idle` status. This closes the native-side observe-only capture,
  not the required stock/native comparison. On June 10, 2026,
  `dist/Deneb_Update_b0fa27b.deneb` refreshed the same native-side proof after
  the shared Cura action-routing work: installed target selftests passed, the
  observe-only `--native --restart --boot-sync --client-proof
  --firmware-proof` summary captured ambient bed/nozzle/topcap telemetry
  around 27.7 C / 30.8 C / 27.0 C with `firmware=none`,
  `machine_type=none`, and no stock `print_service.py`, and the target
  verifier accepted the client/firmware/idle evidence. This remains
  native-side evidence only.
- [x] Carry native firmware/version and topcap telemetry through the web/API
  backend state and shared printer response formatter, so clients consuming
  `/api/v1/printer`, `/api/v1/airmanager`, or Deneb's cached status JSON do not
  discard fields that `deneb-printsvc` has parsed and serialized.
- [x] Port bounded G-code streaming for `JOB`, `MACRO`, and raw `GCODE` without loading whole print jobs into RAM.
- [x] Add shared native stock-derived G-code rewrite coverage for `M117`,
  `M109`, `M190`, and `G280`. Native `gcode_rewrite.*` skips display-only
  `M117`, converts `M109` to `M104` and `M190` to `M140`, expands `G280`
  prime commands using the stock Python sequence shape, and marks streamed
  wait commands so `deneb-printsvc` waits in service code instead of blocking
  old Marlin. Host tests cover raw command rewrite, stream expansion, a
  job-level `M190` wait before the next print move is streamed, raw multi-line
  `GCODE` wait-before-next-command sequencing, and macro-level heater wait
  callbacks. Hardware heat/motion proof remains open.
- [x] Ship Deneb-owned macro defaults under `/etc/deneb/marlindriver/gcode/`
  and fail closed when a macro is missing instead of falling back to
  `/home/cygnus/marlindriver/gcode/`.
- [x] Start abort, pause, resume, and print-finish behavior as deliberate Deneb
  designs instead of blindly copying stock motion sequences. Native active
  pause/resume now has a host-tested physical policy based on stock Python's
  M114/save-position, wipe/retract, park/cool, reheat, restore-position, E, and
  R0 flow, while keeping Deneb's safer clamped build-volume moves.
- [x] Remove stock Python's unsafe abort cleanup from the native design: no
  duplicate homing and no unsafe XY motion into unknown print geometry. Native
  abort deliberately deviates from stock Python's XY/Z homing cleanup.
- [ ] Prove native abort status transitions on hardware after active-print and
  preheat aborts: cancellation must remain visible while cleanup is pending,
  then settle to idle with Stop disabled, no stale `native_active`, no stale
  print filename/UUID/source, no stale temperature targets, and no manual
  cluster cleanup. On June 9, 2026, a dirty `b15786e` resync build passed
  bounded Z-only active-abort and preheat-abort smoke verification on hardware:
  both active snapshots reported `printing` with `native_active:true` and
  `native_stop_allowed:true`, the API abort returned success, and final
  snapshots returned to `idle` with `native_active:false`,
  `native_stop_allowed:false`, blank filename, cleared temperature targets,
  and no stock `print_service.py`. Keep this open until the smoke harness also
  captures the intermediate native `aborting` state while cleanup drains and a
  representative supervised print path repeats the result. A later June 9,
  2026 run with the stricter abort-evidence harness first exposed the real API
  bug behind this open item: preheat abort briefly collapsed to `idle` while
  native cleanup was still active. The shared native status parser/API label
  path now preserves native `aborting` for ABORT frames with
  `denebActive:true`, keeps `is_printing:true` so clients block new actions
  during cleanup, and keeps `native_stop_allowed:false` after the abort request
  is accepted. On hardware, `/tmp/deneb-printsvc-smoke-status-fix-preheat.summary`
  passed: preheat was `printing` with Stop allowed, immediate abort was
  `aborting` with `native_active:true` / `native_stop_allowed:false`, cleanup
  then settled to `idle` with active/stop false, blank filename, cleared heater
  targets, and no manual cluster cleanup.
- [x] Keep native print completion in an active printing state until finish
  cleanup drains in host lifecycle coverage. End-of-file no longer marks the
  job idle immediately; the streamer dispatches the finish policy, keeps
  stop/status semantics active while planner-drain work is in flight, and only
  completes the lifecycle after the flow window clears.
- [x] Preserve startup motion-controller firmware verification/programming behavior or document and test a safer replacement.
- [x] Replace the Deneb printserver handoff with native ownership so Deneb
  clients do not route back to stock `printserver` through a config flag.
- [x] Add native diagnostics logging with stock-shaped status fields beside
  native serial ACK/reject rates, resend counts, queue depth, planner
  starvation indicators, and command latency so future stock/native comparisons
  have a consistent target-side format.
- [x] Add host tests for CRC, packet framing, status parsing, G-code stream replacement, command parsing, and state transitions.
- [x] Add native flow-control regression coverage for late acknowledged
  resends, compact CRC ACK/reject packets, stale firmware resend requests, and
  old-Marlin ProtoError sequence mismatches. Idle and cleanup phases can resync
  to the controller's expected sequence. Live active-print evidence on June 9,
  2026 showed that treating active-job ProtoError resync as fatal caused a
  premature native abort while accepted Z moves were still in flight, so the
  service context now permits sequence resync while native job or raw-G-code
  streaming owns the controller. Host tests prove active service resync keeps
  the job in `printing`; lower-level runtime tests still prove resync remains
  fatal when the service context has not explicitly allowed it.
- [ ] Add live-device smoke tests for boot sync, idle status, client API/bridge
  surfaces, heat/cool, home, macro execution, USB/local print, Cura-started
  print, pause, resume, abort during preheat, abort during active printing,
  print completion, and recovery after service restart.
  Current evidence is partial, not full-matrix closure. June 9, 2026
  `dist/Deneb_Update_d0b61f7.deneb` passed a supervised native
  pause/resume/abort smoke using an all-axis prehome and a generated
  absolute-mode bounded Z fixture:
  `/usr/bin/deneb-printsvc-smoke --physical-ok --native --job
  /tmp/deneb-pause-z.gcode --pause-resume --prehome-action home`, verified by
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --job --pause-resume
  /tmp/deneb-printsvc-smoke-pause-resume-home.summary`. The accepted run showed
  native route ownership, no stock `print_service.py`, `printing` with Stop
  allowed, `paused` with Stop allowed, resumed `printing`, `aborting` with Stop
  disabled while cleanup drained, final `idle` with `native_active:false` and
  `native_stop_allowed:false`, live ambient temperatures, and no
  `POSITION_ERROR`, `macro failed`, or print-ended-with-error log lines. This
  closes the bounded native pause/resume smoke slice only; Cura-started
  pause/resume, representative slicer geometry, stock/native resource
  comparison, and full smoke matrix remain open. The later `34518e8`
  supervised Cura cluster upload/start/abort run closes only the bounded
  Z-only Cura cluster start/abort slice: `/tmp/deneb-cura-z.gcode` was
  generated on-device, started through the cluster API, verified as
  `printing` with `native_active:true` / `native_stop_allowed:true`, aborted
  through the cluster API, observed as `aborting` with Stop disabled while
  cleanup drained, and settled back to `idle` with active/stop false. On
  June 10, 2026, `Deneb_Update_b0fa27b` added a current bounded Z-only
  completion/resource smoke: `/usr/bin/deneb-printsvc-smoke --physical-ok
  --native --complete-job /tmp/deneb-b0fa27b-complete-z.gcode
  --prehome-action z_home` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --complete-job
  --resources /tmp/deneb-printsvc-b0fa27b-complete.summary`. The run used a
  mandatory Z-home safety plan, no heat/extrusion/X/Y motion, showed active
  `printing` with `native_active:true` and `native_stop_allowed:true`, then
  final `idle` with both flags false and blank filename. It also recorded
  native driver RSS samples around 1.6 MiB and throughput of 1320 bytes over
  42 seconds. A final API diagnostic recorded one sequence-number resync after
  the job was already idle; `logread` showed no `POSITION_ERROR`, endstop,
  homing, or fault lines for the accepted run. This closes only the bounded
  native completion/resource smoke slice.
- [ ] Capture supervised live active-print abort evidence on native
  `deneb-printsvc`. Earlier 2026-06-08 evidence is not sufficient anymore:
  later live work exposed active/abort ProtoError desync and stale native
  status risk. The next accepted evidence must show native route ownership,
  active `printing` with Stop allowed, native `aborting` while cleanup drains,
  final idle with `native_active:false` and `native_stop_allowed:false`, no
  unsafe XY-home cleanup lines, no fault markers, and no manual recovery.
  June 9, 2026 `Deneb_Update_be6a5b7` evidence now covers a bounded Z-only
  active-abort fixture: native route owned the driver, active `printing` had
  Stop allowed, API abort returned success, and final state was `idle` with
  `native_active:false` / `native_stop_allowed:false`; Z ended at 202.6 after
  starting from homed 207.0. Keep this open until a representative supervised
  print path also proves no unsafe X/Y cleanup or fault markers.
  A later June 9, 2026 dirty `b15786e` resync build first reproduced the stale
  active-status risk on a 300-cycle bounded Z-only active-abort fixture: the
  first run moved Z but had already fallen back to `idle` before the smoke
  harness sent abort, because active ProtoError resync was treated as fatal.
  After allowing active service-owned sequence resync, the same fixture passed:
  native route owned the driver, the active snapshot was `printing` with Stop
  allowed, `job_line_number` advanced to 50, API abort returned success, and
  the final state was `idle` with `native_active:false` /
  `native_stop_allowed:false`; Z ended at 197.8 after starting from homed
  207.0. The June 9 status-label fix reran that bounded Z-only fixture with
  immediate abort evidence in
  `/tmp/deneb-printsvc-smoke-status-fix-active.summary`: active print was
  `printing` with Stop allowed, immediate and draining snapshots were
  `aborting` with `native_active:true` / `native_stop_allowed:false`, final
  state was `idle` with active/stop false and blank filename, and a log/summary
  grep found no `G28 X/Y/Z` or X/Y cleanup moves. Keep this open for a
  representative supervised print path. The later `34518e8` Cura cluster
  upload/start/abort run repeated the abort-state proof through the cluster API
  on a bounded Z-only fixture, with no heat, no extrusion, no X/Y motion, final
  idle, and Stop disabled after abort; representative Cura/slicer geometry
  remains open. The smoke verifier and stock/native comparator now reject weak
  generic-job or Cura-cluster abort evidence unless abort-requested captures
  scalar `aborting` with `native_active:true` / `native_stop_allowed:false` and
  abort-draining either remains in that aborting state or has already settled
  to idle with active/stop false.
- [x] Add a hard safety interlock to the live smoke harness so heat, homing,
  macro motion, print starts, abort-path jobs, Cura jobs, and completion jobs
  refuse to run unless `--physical-ok` or
  `DENEB_PRINTSVC_SMOKE_PHYSICAL_OK=1` is set. The interrupted full-matrix run
  is not counted as complete evidence.
- [x] Add a second safety interlock for bundled live smoke runs: more than one
  physical phase in a single invocation now refuses to run unless
  `--physical-bundle-ok` or `DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK=1` is set,
  keeping future validation split into narrow supervised checks by default.
- [x] Add guarded pre-home requirements to live smoke phases that can move after
  setup: macro, local-job, web job, preheat-abort, active-abort, Cura job, and
  completion-job now run a `z_home` pre-home step before continuing and write
  `reason=pre_physical_home` summary evidence. All-axis `home` is available
  only through an explicit `--prehome-action home` override for supervised
  tests with a clear X/Y travel path. Pause/resume job smoke now requires that
  all-axis `home` override because the stock-derived pause/resume policy moves
  X/Y while parking and restoring. This is a harness guardrail, not proof that
  every moving phase is physically safe.
- [x] Re-audit live smoke physical-motion safety per axis before accepting any
  more motion evidence. The smoke harness now records a mandatory
  `phase=*-safety kind=physical` line before each physical phase, including the
  axes involved, required homing action, expected travel/range, and stop
  conditions. The verifier and native audit selftests reject full summaries or
  packages that omit this safety-plan evidence. This is a harness contract only;
  live heat, motion, Cura, pause/resume, abort, completion, and stock/native
  resource evidence still remain open.
- [ ] Require before/after RAM, CPU, boot-time, and print-throughput measurements before this replacement can ship outside experimental builds.
  Current native-side-only evidence exists, but the required paired
  stock/native comparison remains open. On June 10, 2026,
  `/tmp/deneb-printsvc-b0fa27b-complete.summary` verified resource mode with
  process RSS samples for `/usr/bin/deneb-printsvc` around 1.6 MiB and a
  completed-job throughput record of 1320 bytes in 42 seconds. This is useful
  native evidence for the reduction gate, not before/after proof.
  Later June 10 `d82245c` paired stock/native resource evidence improved this
  item but still does not close it: `/tmp/deneb-stock-resources-final.summary`
  and `/tmp/deneb-native-resources-final.summary` passed their verifiers, and
  strict comparison showed native improvements in free memory, driver RSS, CPU
  interval, and boot-sync elapsed time. The same strict comparison still failed
  throughput, with stock at 41 B/s and native at 31 B/s, so the
  non-experimental resource gate remains unchecked.
  The current proof ledger records the later dirty `cd5eeba` scheduler fix,
  including stock-matched bounded throughput and the still-open strict
  non-experimental gate.
- [x] Remove the stock `printserver` fallback flag from Deneb's print-control
  route so native `deneb-printsvc` owns the driver path during experimental
  validation.

Completed implementation slices:

- [x] Add a dedicated native `printsvc/` source tree instead of folding the
  rewrite into UI or web code.
- [x] Keep first-stage source files split by responsibility: service init, ZMQ
  IPC, command parsing, status serialization, Marlin status parsing, serial
  transport, flow control, packet framing, CRC, G-code streaming, heater waits,
  macro lookup, and tests.
- [x] Add host tests for command parsing, CRC/packet framing, macro path
  safety, flow control ACK/resend handling, status serialization, Marlin status
  parsing, G28/home-distance telemetry, heater target readiness, and abort
  state cleanup.
- [x] Complete stock Python source parity review item-by-item. The review is
  captured in
  [docs/PRINTSVC_STOCK_PARITY_REVIEW.md](docs/PRINTSVC_STOCK_PARITY_REVIEW.md):
  it maps the stock ZMQ IPC/status contract, sync/status polling,
  firmware/version parsing, G-code rewrite behavior, completion callback
  timing, abort cleanup timing, pause/resume position capture, and datalink
  resend behavior to native owners and tests. It explicitly records the native
  divergences: bounded diagnostic `M115`, safer no-XY-home abort cleanup, and
  finish cleanup that stays active until planner-drain evidence. Native pause
  now waits for a fresh `M114` position report before saving the restore point,
  with host tests covering stale-position rejection. Live stock/native firmware,
  representative slicer geometry, client-flow, and resource comparisons remain
  separate open evidence items.
- [x] Add a native serial response pump that reads Marlin lines, updates parsed
  status fields, accounts ACK/reject/resend responses, and re-emits resend
  packets through the flow-control queue.
- [x] Move `JOB` handling out of the blocking command-reply path so
  `ABORT`/`PAUSE`/`RESUME` commands can still be accepted while a job is
  queued, preheating, or streaming.
- [x] Add a native `print_control` contract module for Deneb-owned print
  phases, stock request strings, action command verbs, active-state decisions,
  and stop-allowed decisions instead of baking those rules into each caller.
- [x] Add a native pending-job metadata module that serializes the Cura-visible
  pre-print/conflict JSON shape, including tracker and material/print-core
  change markers, so that format can move out of temporary bridge code.
- [x] Add a shared native pending-job file helper used by LCD `backend_comm`,
  web `backend_zmq`, web `api_print_job`, web `api_cluster`, touchscreen
  conflict UI, and `deneb-printsvc` tests so pending-job path, tracker,
  conflict, handled-state parsing, and cleanup path ownership have one
  Deneb-owned implementation instead of several local string scans/macros.
- [x] Move pending-job metadata cleanup into `common/print/pending_job_file.*`
  so LCD Stop, touchscreen conflict cancel/fallback, web stop/abort, Cura
  cluster cancel/delete, and print-end cleanup all clear the shared pending
  file through one native helper instead of direct path deletes.
- [x] Add a shared native print-state rules helper used by LCD `backend_comm`,
  web `backend_zmq`, and `deneb-printsvc` tests so request-name classification,
  preheat/temperature-target activity, transient macro-file filtering, and
  abort/paused detection stay aligned across touchscreen and web/API clients.
- [x] Move web/API status labels and active-job decisions for UM API,
  cluster API, and backend status cache onto the same shared print-state rules
  so `"idle"`, `"offline"`, `"printing"`, `"paused"`, `"error"`, and
  `"finished"` are not reinterpreted separately by each REST surface.
- [x] Move stop-request debounce/in-flight ownership into shared print-state
  rules so touchscreen Stop and web/API Stop use one tested guard for duplicate
  stop suppression, active abort tracking, and idle reset instead of separate
  local timing rules.
- [x] Move the stock empty print value `"none"` into shared print-state rules
  so status parsing, native status serialization, pending display fallbacks,
  command job-path extraction, diagnostics logging, and touchscreen status
  display agree on empty file/job semantics.
- [x] Move flat stock-status JSON field extraction into
  `common/print/json_field.*` so LCD `backend_comm` and web `backend_zmq`
  parse numeric and string status fields through the same native helper
  instead of maintaining separate mini JSON readers for the Python driver's
  status payload.
- [x] Move print-job list file fallback reads into shared native helpers:
  `common/print/json_file.*` owns safe JSON-array-or-empty loading for pending
  and history files, while `common/print/print_history.*` owns the Deneb print
  history path used by web API reads and backend print-end history writes.
- [x] Move print-history append/write ownership into
  `common/print/print_history.*` so web backend print-completion handling no
  longer hand-rolls history JSON construction, escaping, array append, temp-file
  rewrite, or timestamp formatting beside the ZMQ status loop.
- [x] Move JSON string escaping into `common/print/json_string.*` so native
  pending-job serialization, native status serialization, and web status-cache
  filename JSON all share one escaping rule instead of carrying local copies.
- [x] Move Cura/UM2C machine, material, and nozzle profile defaults into
  `common/print/print_profile.*` so native pending metadata, Cura cluster
  printer/job responses, UM printer hotend responses, and upload conflict
  detection use the same machine family/variant, default PLA GUID/color/brand,
  loaded UCI material/nozzle reads, nozzle-id normalization, and simple
  material-name resolution.
- [x] Extend `common/print/json_field.*` to own API JSON field presence,
  strict numeric parsing, and boolean parsing so Deneb auth/setup and UM
  printer motion/action request bodies no longer carry separate miniature JSON
  parsers beside the stock print-status parser.
- [x] Move UM printer temperature request parsing onto
  `common/print/json_field.*` so bed/nozzle heat APIs use the same strict
  native float parser as motion controls before formatting shared G-code.
- [x] Move stock-status truthy value parsing into `common/print/json_field.*`
  so LCD and web/API status readers interpret fields such as
  `topcapIsPresent` consistently across numeric, `"yes"`, `"true"`, and
  shorthand truthy payloads.
- [x] Move strict integer JSON field parsing into `common/print/json_field.*`
  and route native print-service command parsing plus pending-job metadata
  loading through the shared JSON and command-verb helpers instead of keeping
  private parsers/string literals beside the driver rewrite.
- [x] Move stock/native status payload field parsing into
  `common/print/status_payload.*` so LCD `backend_comm`, web `backend_zmq`,
  and native tests share one owner for temperature, position, job identity,
  topcap, fault, progress, pause, and active-print derivation from ZMQ JSON.
- [x] Move stock/native active filename resolution into
  `common/print/status_payload.*` so LCD `backend_comm`, the LCD status
  screen, and web `backend_zmq` share pending-job fallback, transient macro
  filtering, retained print-name behavior, and abort cleanup instead of
  exposing temporary macro filenames during preheat or conflict continuation.
- [x] Move active filename context construction into
  `common/print/status_payload.*` so LCD status rendering, LCD backend status
  parsing, and web backend status parsing do not each hand-build the same
  print context when resolving retained job names.
- [x] Move native ZMQ status-to-backend-state application into
  `common/print/status_state.*` so LCD `backend_comm` and web `backend_zmq`
  share parsed-field copying, retained filename resolution, stop-guard cleanup,
  timing normalization, and active/preparing/stoppable context helpers instead
  of reinterpreting the same status payload independently.
- [x] Add host regression coverage for `common/print/status_state.*` so native
  status consumers keep active/preheat stop allowance, retained print-name
  cleanup, firmware/topcap identity fields, context flags, and idle stop-guard
  clearing aligned after active or aborting jobs transition back to idle.
- [x] Move uploaded print-file metadata sniffing into
  `common/print/print_job_file.*` so Cura upload registration, conflict
  detection, and future native print-service entry points share one parser for
  `material_guid`, `nozzle_size`, `print_core_id`, and the Deneb upload spool
  path instead of keeping that logic inside the web print-job endpoint.
- [x] Move uploaded print-file spool ownership into
  `common/print/print_job_file.*`: filename sanitizing, Deneb spool-path
  construction, spool directory creation, and rename/copy fallback are shared
  native helpers instead of web-only Cura upload code.
- [x] Move Cura upload storage planning into
  `common/print/print_job_file.*` so the web upload endpoint receives one
  sanitized filename and destination path from native print-file policy before
  pending-job dedupe, file storage, registration, and accepted-response
  formatting.
- [x] Move multipart upload extraction out of `web/src/main.c` and into the
  named native web API module `web/src/api_multipart.c` so Cura print uploads
  and material uploads share upload parsing without making the server loop a
  catch-all implementation file.
- [x] Move printer hostname/GUID identity reads into
  `common/print/printer_identity.*` so Cura cluster printer/job responses and
  UM system endpoints use the same native fallback/default behavior instead of
  each carrying a local command/file reader.
- [x] Move the touchscreen conflict prompt's continue/cancel actions off the
  embedded Python coordinator launcher and onto native backend `JOB`/`ABORT`
  commands for the pending print path.
- [x] Move the Cura cluster API pending-job continue/cancel actions off the
  embedded Python coordinator launcher and onto native web backend `JOB`/`ABORT`
  commands for the pending print path.
- [x] Move Cura upload registration out of the embedded Python/Gershwin
  coordinator launcher: `deneb-api` now assigns a native tracker, writes
  native pending-job metadata from UCI/file hints, leaves conflict jobs waiting
  for user confirmation, and starts no-conflict jobs with a native `JOB`
  command.
- [x] Move Cura upload pending-registration planning into
  `printsvc/src/pending_job_registration.*` so web upload handling and future
  native print-service entry points share one owner for profile comparison,
  tracker assignment, pending metadata population, and immediate-start
  decisions.
- [x] Move Cura upload immediate-start dispatch into
  `printsvc/src/pending_job_registration.*` so the web upload endpoint no
  longer owns the pending-vs-start callback sequence after native registration.
- [x] Remove remaining native UI Python launchers from frame lighting, USB
  material-profile import, and diagnostics export: frame lighting sends native
  `M142` G-code directly, material import scans USB/profile XML in C, and log
  export archives with `tar` instead of Python `shutil`.
- [x] Move diagnostics USB mount probing and support-export shell command
  construction into `common/print/diagnostics_export.*` so the LVGL diagnostics
  screen no longer owns stock log paths, Deneb log inclusion, redaction filters,
  archive naming, or background export log redirection locally.
- [x] Move native command formatting for stock `GCODE`, `MACRO`, `JOB`, and
  action frames into `common/print` with parser round-trip tests, and wire LCD
  `backend_comm`, web `backend_zmq`, and `deneb-printsvc` through that shared
  owner for G-code, macro, job, and action frame escaping.
- [x] Move generic stock `COMMAND<payload` frame construction into
  `common/print/command_format.*` so LCD and web backend clients no longer
  each hand-format raw command frames for non-action verbs.
- [x] Move generic `JOB` argument path/file fallback extraction into the shared
  native command-format helper so LCD `backend_comm` no longer keeps a local
  JSON field helper just to retain active job filenames.
- [x] Move backend command-frame planning into
  `common/print/command_format.*` so LCD and web transports share the same
  decision for simple action frames, raw payload frames, and `JOB` path
  extraction before applying only their local filename-retention side effects.
- [x] Move stock print-service command verb names for `GCODE`, `MACRO`, `JOB`,
  `ABORT`, `PAUSE`, and `RESUME` into `common/print/command_format.h` so
  action-frame formatting, LCD backend commands, web backend commands, and
  native `print_control` do not each carry separate raw driver strings.
- [x] Split shared print-state action parsing/planning into
  `common/print/print_action_rules.*` and shared ASCII string matching into
  `common/print/print_string.*`, keeping status/context rules separate from
  REST/Cura action interpretation while preserving one native helper API for
  touchscreen, web/API, and `deneb-printsvc` callers.
- [x] Move field-level active/preparing/stoppable print-context derivation into
  `common/print/print_state_rules.*` helpers so LCD and web/API backend ZMQ
  clients no longer each rebuild the same observation wrapper before applying
  shared native state rules.
- [x] Move field-level active filename context construction into
  `common/print/status_payload.*` so LCD and web/API backend ZMQ clients no
  longer each open-code the same status filename retention context before
  resolving transient macro names, pending-job names, or retained print names.
- [x] Split print timing/progress math into
  `common/print/print_timing_rules.*` so lifecycle/context rules, action
  planning, filename retention, and progress normalization remain separate
  named native source units.
- [x] Move direct touchscreen/web macro, multi-line G-code, and job-start
  callers onto native backend helper functions so build-plate leveling, jog
  motion, material load/unload, touchscreen print start, touchscreen conflict
  continue, web motion macros, Cura upload start, and Cura pending-job continue
  no longer each hand-roll stock `COMMAND<json>` payloads.
- [x] Move repeated touchscreen/web jog, absolute-position, Z-home, heater
  target, cooldown, and material load/unload G-code string construction into
  `common/print/gcode_command.*` with host tests so remaining raw G-code
  commands used by UI/API safety gates share one Deneb-owned formatter.
- [x] Move bed/nozzle temperature command limits into
  `common/print/gcode_command.*` so web/API temperature writes, touchscreen
  temperature sliders, and material-workflow target sliders clamp against one
  shared native hardware limit owner.
- [x] Deduplicate web/API bed and nozzle heater write dispatch and invalid
  temperature response mapping so both REST endpoints use one native
  heater-target planning path before sending the backend G-code command.
- [x] Move touchscreen material load/unload command sequencing into
  `common/print/gcode_command.*` so reset-extruder, extrude/retract distance,
  load/unload feedrates, and movement timeout math live with the shared native
  material G-code owner instead of the LVGL screen.
- [x] Move relative jog command sequencing into `common/print/gcode_command.*`
  so touchscreen and web/API jog controls both send the same `G91`, bounded
  move, and `G90` sequence instead of each preserving that safety ordering
  locally.
- [x] Move web/API manual motion bounds into `common/print/gcode_command.*`
  so jog distance, axis normalization, build-volume limits, and move-speed
  validation are owned beside the G-code formatter instead of inside the REST
  endpoint.
- [x] Move touchscreen frame-lighting `M142` command construction and
  brightness-to-PWM clamping into `common/print/gcode_command.*` so another
  UI-only stock-driver G-code formatter is owned by the shared native layer.
- [x] Move touchscreen frame-light saved-state defaults, legacy UCI fallback,
  output-brightness selection, and stock/Deneb UCI save command formatting into
  `common/print/frame_light.*` so the LVGL settings screen no longer owns
  frame-light persistence compatibility rules locally.
- [x] Move diagnostics Air Manager fan `M12030` command construction into
  `common/print/gcode_command.*` so maintenance diagnostics no longer embeds a
  stock-driver raw G-code literal in the LVGL screen.
- [x] Move native abort/finish motion-policy heater/fan/stepper cleanup onto
  shared G-code constants and heater-off helpers so print-service safety
  cleanup uses the same cooldown command owner as touchscreen/web controls.
- [x] Move cooldown command sequencing into `common/print/gcode_command.*` so
  touchscreen cooldown and native abort/finish motion policies share nozzle-off,
  bed-off, and fan-off ordering instead of assembling those safety commands in
  separate modules.
- [x] Move stock macro filenames and remaining pending-job display reads into
  shared native helpers: touchscreen jog/leveling and web motion commands now
  use common macro-name constants, the shared print-state rules own transient
  macro-file filtering, and LCD/API job-name display reads pending-job metadata
  through `pending_job_file` instead of local JSON scans.
- [x] Add a safer native macro resolution policy: `deneb-printsvc` now rejects
  path traversal and non-`.gcode` macro names, treats the Deneb-owned
  `/etc/deneb/marlindriver/gcode` directory as `DENEB_PRINTSVC_MACRO_DIR`,
  and fails closed when a macro is missing instead of falling back to stock
  `/home/cygnus/marlindriver/gcode` files.
- [x] Package original Deneb macro defaults for native `MACRO` execution:
  homing/parking, manual build-plate jogs, build-plate leveling positions,
  finish cleanup, material feed/retract helpers, and frame-light toggles now
  install under `/etc/deneb/marlindriver/gcode` so normal native macro
  execution does not depend on stock `marlindriver` macro files, with tests
  covering Deneb macro resolution when no stock macro directory is available.
- [x] Move touchscreen USB/local print-file browser roots and file candidate
  filtering onto shared native print helpers so the LVGL print screen no longer
  hand-checks `.gcode`/`.ufp` suffixes or embeds the stock `/mnt/sda1` and
  `/home/3D` scan paths locally.
- [x] Move material-profile USB import root, recursion-depth limit, candidate
  suffix filtering, and recursive import scanning into
  `common/print/material_catalog.*` so the LVGL material-set screen no longer
  owns stock material import policy beside the native material catalog
  parser/storage helpers.
- [x] Move native print-file start planning into
  `common/print/print_job_file.*` so touchscreen USB starts and web/API
  immediate Cura starts share path/source/UUID/default-temperature semantics
  before sending a backend `JOB` command.
- [x] Route pending-job continue dispatch through the same native print-file
  start plan so touchscreen conflict continuation and Cura cluster continuation
  preserve pending path/source/UUID metadata without each hard-coding zero
  temperature targets beside the transport call.
- [x] Move touchscreen conflict prompt metadata extraction into
  `common/print/pending_job_file.*` so job/material display defaults,
  pending-state detection, and conflict flags are not reinterpreted inside the
  LVGL screen.
- [x] Move default pending/conflict metadata existence checks into
  `common/print/pending_job_file.*` so Cura cluster action routing and the
  touchscreen conflict trigger do not each load and classify the pending file
  locally.
- [x] Move Cura cluster pending-job continue/cancel action planning into
  `common/print/print_state_rules.*` so conflict-resolution dispatch commands,
  failure messages, and pending-dispatch cleanup boundaries are owned by
  shared native print-control/dispatch helpers instead of `api_cluster`.
- [x] Move print-action parse/unknown response bodies into
  `common/print/print_state_rules.*` so Cura cluster and UM print-job endpoints
  do not preserve separate API message literals for the same native
  pause/print/abort action contract.
- [x] Move normal print-job pause/resume/abort/stop dispatch selection into
  `common/print/print_action_dispatch.*` so REST endpoints provide only
  backend transport callbacks while shared native code owns action-plan
  execution and pending-cleanup triggering.
- [x] Move Cura cluster print-job delete planning into shared native action
  rules/dispatch so active-job abort versus idle pending-file cleanup is not
  decided inside `api_cluster`.
- [x] Move manual motion action planning into `common/print/manual_motion.*`
  so touchscreen jog buttons and web/API `home`/`z_home`/`bed_up`/`bed_down`
  actions share one native owner for whether a motion action sends a macro or
  direct G-code.
- [x] Move web/API manual-motion action request parsing into
  `common/print/manual_motion.*` so action JSON shape, the legacy home
  fallback, unknown-action classification, and macro-vs-G-code selection are
  not split between REST endpoints and touchscreen helpers.
- [x] Move web/API motion-plan and manual-motion error response mapping into
  `common/print/gcode_command.*` and `common/print/manual_motion.*` so REST
  endpoints no longer keep their own copies of jog, absolute-position, and
  motion-action validation messages beside the native planners.
- [x] Move build-plate leveling macro planning into
  `common/print/buildplate_level.*` so the touchscreen leveling workflow no
  longer owns the stock leveling macro filename sequence in LVGL screen code.
- [x] Move pending-job display-name fallback rules into
  `common/print/pending_job_file.*` so LCD backend status retention,
  touchscreen status labels, native pending-job metadata, and web/API printer
  responses all prefer the same pending name, path basename, and `"none"`
  filtering rules.
- [x] Move pending-job continue/cancel action planning into
  `common/print/pending_job_file.*` so Cura cluster actions and touchscreen
  conflict prompts share the same native decision for `PREPARE` -> `JOB`,
  `ABORT`, handled-state marking, and metadata cleanup while keeping only the
  transport send in each client.
- [x] Move pending-job action success completion into
  `common/print/pending_job_file.*` so Cura cluster actions and touchscreen
  conflict prompts share one native owner for mark-handled versus clear-after-
  abort behavior after their backend command succeeds.
- [x] Move pending-job continue/cancel backend dispatch behind LCD/web backend
  adapter helpers so touchscreen conflict prompts and Cura cluster actions no
  longer duplicate the load, plan, `JOB`/`ABORT` send, and finish-action flow.
- [x] Move pending-job continue/cancel dispatch sequencing into
  `common/print/pending_job_dispatch.*` so LCD and web backend adapters share
  the load, plan, start-readiness check, `JOB`/`ABORT` callback selection, and
  finish-action timing while keeping only the transport send callbacks local.
- [x] Move pending-job presence/path/conflict predicates into
  `common/print/pending_job_file.*` so web upload dedupe, Cura cluster pending
  actions, and touchscreen conflict prompts use one tracker/path/conflict
  contract instead of local `tracker >= 0` and `path[0]` checks.
- [x] Route Cura cluster pending-action detection and touchscreen conflict
  prompt availability through the shared pending-job presence predicate so
  tracker values remain metadata instead of duplicated control-flow gates.
- [x] Move pending-upload dedupe/block decisions into
  `common/print/pending_job_file.*` so the web print upload endpoint reuses the
  same native pending-job path/display-name comparison when accepting a
  duplicate Cura upload or rejecting a different queued job.
- [x] Move default pending-upload lookup plus dedupe/block classification into
  `common/print/pending_job_file.*` so upload callers no longer separately load
  the default pending metadata before asking the shared helper for duplicate or
  blocked decisions.
- [x] Move manual-action safety gating into shared native print-state rules so
  web/API motion controls, touchscreen jog controls, temperature actions,
  material moves, and frame-light startup apply all use one connected,
  not-printing, not-paused, not-error predicate.
- [x] Move print-start readiness gating into shared native print-state rules so
  touchscreen USB starts, Cura immediate starts, and pending-job continuation
  all reject new `JOB` dispatch while disconnected, errored, paused, or already
  printing through one Deneb-owned policy.
- [x] Move touchscreen status-screen display-state selection into shared native
  print-state rules so Cooling, Paused, Preparing, Printing, Error, and Idle
  labels follow the same Deneb-owned status policy instead of a local LVGL
  decision tree.
- [x] Move LCD/web manual-action readiness checks behind backend adapter
  helpers so jog, temperature, material, frame-light startup, USB print start,
  and web motion endpoints no longer inspect cached connection/error/print
  booleans directly before deciding whether hardware actions are allowed.
- [x] Move temperature-target readiness into shared native print-state rules so
  preheat logging, native heater waits, and material move readiness agree on
  bed-only, nozzle-only, combined-target, and tolerance behavior.
- [x] Move backend preheat transition tracking into shared native print-state
  rules so LCD and web/API ZMQ clients emit preheat-target-active and
  targets-ready transitions from one tested tracker instead of separate local
  `preheat_targets_logged` / `preheat_reached_logged` flags.
- [x] Move LCD/web backend lifecycle transition classification into shared
  native status-state helpers so pause/resume, start/end, completion/error,
  and preheat-event logging use one tested transition owner while adapter code
  keeps only transport, history, and display side effects.
- [x] Move material-move minimum temperature and ready-window policy into
  `common/print/print_state_rules.*` so the touchscreen material workflow no
  longer owns its own hotend movement safety threshold beside shared heat-state
  rules.
- [x] Move material workflow default temperature and stop/cooldown planning into
  `common/print/material_workflow.*` so the touchscreen material screen no
  longer owns stock-driver `M401` stop-material or nozzle-off command details.
- [x] Move material workflow status selection into
  `common/print/material_workflow.*` so Busy, Moving, Set Target, Cooling,
  Target Too Low, Ready, and Heating labels follow one native policy instead of
  a local LVGL decision tree.
- [x] Move print elapsed-time calculation into shared native print-state rules
  so UM API, Deneb API, and Cura cluster API responses clamp `time_total` /
  `time_left` consistently instead of each computing elapsed seconds locally.
- [x] Move print progress percentage calculation into shared native print-state
  rules so LCD and web/API backend status parsing clamp weird `Tleft` values
  the same way during preheat, printing, completion, and abort cleanup.
- [x] Move Deneb system language choices, UCI read fallback, validation, and
  save-command formatting into `common/print/system_language.*` so touchscreen
  language settings and web/Deneb config endpoints no longer duplicate local
  language lists or raw UCI read strings.
- [x] Move touchscreen About printer-id lookup into
  `common/print/printer_identity.*` and route the full web system payload
  through shared language reads so UI/API identity and language fields no
  longer preserve one-off shell or hard-coded fallbacks.
- [x] Move backend status time/progress normalization into shared native
  print-state rules so LCD and web/API ZMQ clients no longer carry separate
  inactive-job zeroing, `time_left` clamping, or progress recomputation logic.
- [x] Move pending-job metadata persistence into the native pending-job module
  so Cura upload registration and future `deneb-printsvc` entry points share
  the same serialize, temp-write, atomic-rename, and cleanup contract instead
  of keeping the file writer inside the web print-job endpoint.
- [x] Move Cura/touchscreen material catalog XML parsing, GUID validation,
  record storage, and dynamic material-list response assembly into
  `common/print/material_catalog.*` so cluster API uploads and USB material
  imports share native material persistence rules.
- [x] Move Cura material-upload store-and-cleanup handling into
  `common/print/material_catalog.*` so the cluster endpoint no longer owns
  temp-file cleanup or default catalog persistence after multipart extraction.
- [x] Tighten native material catalog version parsing so malformed Cura
  material XML versions are rejected by `common/print/material_catalog.*`
  instead of being silently coerced by local `atoi` behavior.
- [x] Move touchscreen loaded material/nozzle reads onto
  `common/print/print_profile.*` so settings screens, Cura conflict detection,
  pending metadata, and UM/Cura API responses all use the same native UCI
  fallback defaults.
- [x] Move supported nozzle-size normalization into
  `common/print/print_profile.*` so the touchscreen settings screen and native
  Cura/print metadata paths share one default/fallback rule for UM2C nozzle
  sizes.
- [x] Move stock material/nozzle selection choices, display labels, and UCI
  write command formatting into `common/print/print_profile.*` so touchscreen
  settings screens no longer embed profile GUID lists or stock option update
  commands locally.
- [x] Move UM API print progress fraction clamping into shared native
  print-state rules so API responses cannot drift from the backend percentage
  contract.
- [x] Move job state-or-none naming into shared native print-state rules so UM
  API and Deneb API current-job responses agree on `"none"`, `"printing"`,
  `"paused"`, and `"error"` behavior, including errored jobs that are no longer
  actively streaming.
- [x] Move web and Cura print-job action parsing into shared native
  print-state rules so plain and JSON `pause`, `print`/`resume`/`continue`,
  `force`, `abort`/`cancel`, and `stop` actions classify the same way before
  reaching LCD/web/backend command dispatch.
- [x] Move the web/Cura action vocabulary itself into shared native
  print-state rules so `pause`, `print`, `resume`, `continue`, `force`,
  `start`, `abort`, `cancel`, and `stop` are not duplicated between the parser,
  cluster pending fallback, and host tests.
- [x] Move Cura cluster pending-action fallback parsing into shared native
  print-state rules so a missing action body only defaults to `print` when a
  pending job exists, instead of keeping that compatibility rule inside the
  REST endpoint.
- [x] Move Cura cluster pending-versus-normal action routing into shared native
  print-state rules (`deneb_print_cluster_action_plan`) so the HTTP adapter no
  longer decides whether `print`/`cancel` should drive pending-job
  continue/cancel or normal resume/abort behavior.
- [x] Move web print-job action planning into shared native print-state rules
  so pause, resume/start, abort/cancel, and stop share one owner for backend
  command selection, failure classification, and pending-job cleanup intent.
- [x] Move web and Cura print-job action dispatch onto one native web helper
  so UM API state changes and cluster API actions share pause/resume/abort/stop
  backend calls, pending-job cleanup, success bodies, and failure messages,
  with only pending-job continue/cancel left as Cura-specific branches.
- [x] Move current-job fallback identity into shared native print-state rules
  so native pending metadata, Cura cluster jobs, UM API, and Deneb API use the
  same default job UUID, display name, and source instead of local literals.
- [x] Move current/queued print-job response summaries into
  `common/print/print_job_summary.*` so UM API, Cura cluster API, Deneb API,
  and upload acceptance responses share one native owner for job activity,
  identity defaults, state, elapsed time, and progress scaling.
- [x] Move UM API and Deneb API current-job JSON response formatting into
  `common/print/print_job_summary.*` so the REST endpoints no longer
  separately assemble active-job fields or diverge on escaping, elapsed time,
  state, and UM fraction versus Deneb percent progress semantics.
- [x] Move UM API current-job scalar response formatting into
  `common/print/print_job_summary.*` so `/api/v1/print_job/name`, `uuid`,
  `source`, `state`, `progress`, and time fields share the same escaping and
  numeric formatting as the full current-job response.
- [x] Move UM printer root/status response formatting into
  `common/print/printer_status_response.*` so `api_printer` no longer owns
  backend-status label quoting, top-level print flags, temperature/position
  field layout, or pending-filename fallback beside the REST transport code.
- [x] Move UM printer bed/head/extruder/hotend/position/feeder subresource
  response formatting into `common/print/printer_status_response.*` so the
  REST endpoint no longer repeats stock printer JSON fragments for every
  resource path.
- [x] Move UM printer material, LED, ambient, and Air Manager compatibility
  response formatting into `common/print/printer_status_response.*` so
  topcap-derived accessory state and stock-shaped printer constants are owned
  beside the other native printer response helpers.
- [x] Move web backend cached-printer-state adaptation behind
  `backend_zmq_get_printer_status_response()` so `api_printer` no longer
  copies backend temperature, position, print flags, topcap, filename, or
  status-label fields into response structs itself before formatting UM
  printer responses.
- [x] Move UM LED scalar response bodies into
  `common/print/printer_status_response.*` so LED brightness/hue/saturation
  compatibility defaults share the same native owner as the full LED response.
- [x] Move Cura cluster active print-job response formatting into
  `common/print/print_job_summary.*` so cluster job metadata, configuration,
  build-plate, owner/printer UUID, and compatible-family response shape are not
  hand-assembled inside the REST endpoint.
- [x] Move Cura upload accepted/already-queued response JSON into
  `common/print/print_job_summary.*` so web upload responses share the same
  escaped queued-job body, progress fraction, elapsed time, and identity
  defaults instead of hand-assembling those fields in the endpoint.
- [x] Move web REST current-job/status adapter calls behind `backend_zmq`
  helper accessors so UM API, Cura cluster API, Deneb API, and printer-status
  endpoints no longer rebuild `printer_state_t` summaries or status labels in
  each endpoint file.
- [x] Move Cura/pending native job-start identity onto the shared print-state
  defaults so upload auto-start, Cura cluster continue, and touchscreen
  conflict continue no longer carry their own `"deneb-current-job"` literals.
- [x] Move touchscreen local USB job-start identity onto the shared print-state
  source/default UUID helpers so direct file-browser starts no longer carry
  local `"USB"` / `"0"` command metadata literals, and native pending metadata
  uses the same shared source default for its Cura owner field.
- [x] Move print completion history labeling into shared native print-state
  rules so web history and future native diagnostics use the same
  `"completed"`, `"stopped"`, and `"error"` decision. Native completion
  history now considers the final native request as well as timing/error state,
  so a short job that reaches `Complete` without a nonzero stock timing window
  is logged as completed instead of stopped.
- [x] Match stock completion callback identity cleanup in native lifecycle:
  after finish policy/drain completes, native status marks completion and
  clears active file/source/UUID/heater targets so clients do not retain a
  stale completed job as active print context. A later live bounded-completion
  run exposed a post-completion flow-control desync; native flow-control faults
  are now fatal only while an active print phase remains, so late protocol
  cleanup after `Complete` no longer flips the final idle state into
  `serial_fault`.
- [x] Move active/preparing/stoppable print-context decisions into shared
  native print-state rules so the touchscreen Stop button, preheat status, and
  abort cleanup use the same tested contract instead of local screen/backend
  predicates.
- [x] Bundle active/preparing/stoppable context flags into one shared native
  print-state helper so LCD and web/API clients do not independently combine
  filename, request, timing, pause, and preheat signals when deciding whether a
  print is still controllable.
- [x] Move print observation construction into shared native print-state rules
  so LCD status rendering, LCD backend status parsing, web backend status
  parsing, and native status parsing do not each hand-build the same
  active/preheat/stoppable context input.
- [x] Move LCD status-screen display-name and print-context access behind
  `backend_comm` helper accessors so the screen no longer carries its own
  retained-name cache, status-payload filename context, or active/preparing/
  stoppable/aborting predicates.
- [x] Move stock print-service request/status vocabulary for `PREPARE`,
  preheat/preheating, idle, printing, paused, aborting, complete, and error
  into shared native print-state rules so native status serialization,
  print-control phase mapping, Cura pending-job continue, and touchscreen
  conflict continue use one contract.
- [x] Move stock UM API compatibility identity/state literals for queued web
  uploads into shared native print-state rules so `WEB_API`, stock response
  UUID `0`, and `pre_print` are not separately owned by web print-job
  responses, pending metadata, and print-control phase names.
- [x] Add native error mapping for Marlin faults and service-side storage,
  serial, command, thermal, and motion categories, with escaped status JSON
  fields for machine-readable Deneb error keys/details.
- [x] Add native motion-controller firmware hash/cache verification and
  lab-gated programming handoff without adding Python to the new driver path.
- [x] Add deliberate native finish/abort motion-policy helpers with tests that
  guard against duplicate or unsafe XY homing during abort cleanup.
- [x] Add the native print-service init handoff: Deneb stops stock
  `printserver` before starting native `deneb-printsvc`, and replaces the
  stock `printserver` init script with a Deneb-owned shim that no longer
  launches the old driver or delegates back through a Deneb config flag. The
  native init and shim also clear stale `/var/run/printserver.pid` state and
  terminate an exact `/home/cygnus/marlindriver/print_service.py` process if it
  survived the stock init stop path.
- [x] Add native pause/resume state-machine tests so paused jobs do not continue
  streaming and preheat-stage pauses resume to preparing instead of pretending
  to be actively printing.
- [x] Add native active-print pause/resume motion-policy coverage: active pause
  saves X/Y/Z/E/R0/nozzle setpoint, queues the stock-derived retract/park/cool
  policy, accepts a resume request while pause cleanup is still draining, and
  blocks job streaming until the reheat/restore policy drains.
- [x] Match stock pause-position callback ordering in native pause control:
  active pause now sends and drains `M114` before saving the restore
  X/Y/Z/E/R0/nozzle state and starting the cleanup policy, with host tests
  proving the job stream remains gated until pause/resume policies drain.
- [x] Move native job-control ownership into
  `printsvc/src/job_control.*` so job acceptance, duplicate-active rejection,
  stream-open storage failures, preheat heater-wait initialization, and abort
  cleanup are not embedded in generic command dispatch.
- [x] Move native pause/resume transition ownership into
  `printsvc/src/pause_resume.*` so the service command dispatcher no longer
  hand-rolls paused/preparing/printing transitions, idle pause/resume commands
  fail explicitly, and host tests cover active, preheat, complete, and error
  cases.
- [x] Move native active-job lifecycle status ownership into
  `printsvc/src/job_lifecycle.*` so accepted, streaming, completed, aborted,
  and storage-error transitions are not embedded directly in the service
  dispatcher/poller, with host tests covering source defaults, request labels,
  abort cleanup, and fault mapping.
- [x] Move touchscreen abort-display context behind shared native status-state
  helpers: LCD `backend_comm` now calls
  `deneb_status_state_has_abort_context` instead of interpreting abort request
  text locally, and host tests cover both native abort status and local
  stop-inflight display context. This thins the LCD adapter but does not close
  the required hardware LCD abort-flow proof.
- [x] Move LCD/web backend transition logging onto shared native status-state
  helpers: `deneb_status_state_transition_from_pair` classifies request
  changes, pause/resume, start/end, and completion labels, while
  `deneb_status_state_preheat_events` wraps the shared preheat tracker for both
  ZMQ clients. The integration audit now rejects either adapter if those helper
  calls disappear.
- [x] Remove the remaining web/API completion and bed-preheat status
  duplication: web print history now consumes the completion label returned by
  `deneb_status_state_transition_from_pair`, and
  `printer_status_response.c` reports UM bed `pre_heat.active` from
  `deneb_print_has_temp_targets` for printer root, `/printer/bed`, and
  `/printer/bed/pre_heat` responses instead of hard-coding inactive JSON. Host
  tests cover active and inactive bed targets, and the integration audit
  rejects client-side completion reclassification or hard-coded inactive bed
  preheat output.
- [x] Route web backend cached status JSON through the same request/native-aware
  status-label accessor used by REST and Cura cluster status, and add an
  integration-audit rejection for direct request-blind status-label calls in
  `backend_zmq`.
- [x] Harden the integration audit against new client-side raw G-code
  patchwork: `deneb-printsvc-integration-audit` now rejects raw motion/heater
  G-code string literals in UI and web/API `.c` client adapters, while keeping
  those literals allowed only in shared/native policy owners such as
  `common/print/gcode_command.*`, `manual_motion.*`, and native
  `printsvc/src/*` modules. The negative-fixture selftest proves both LCD and
  web/API regressions fail closed.
- [x] Harden the integration audit against pending-job state bypasses: UI and
  web/API client adapters now fail the source audit if they reference
  `/tmp/deneb-cluster-print-job.json` or `DENEB_PENDING_JOB_PATH` directly
  instead of going through `common/print/pending_job_file.*`,
  `pending_job_dispatch.*`, or native pending-job registration owners. The
  negative-fixture selftest proves both LCD and web/API bypasses fail closed.
- [x] Make native abort/finish cleanup policy failures visible as serial faults
  when motion transport is marked ready, so the driver no longer reports
  successful abort or completion if the required cleanup G-code cannot be sent.
- [x] Reject native service-level abort commands while idle without sending
  cleanup motion or leaving `abort_requested` latched, while still allowing
  stale active status to be cleaned back to idle.
- [x] Move native active-job streaming ownership into
  `printsvc/src/job_streamer.*` so preheat gating, paused/abort checks,
  bounded line streaming, finish policy dispatch, and planner-starvation
  accounting are one tested polling policy instead of inline service-loop
  logic.
- [x] Make active-job stream send failures terminal and visible: failed native
  G-code output now closes the stream, clears active-job state, and records a
  command or serial lifecycle error instead of leaving the status looking like
  the job is still printing.
- [x] Route job-streamer abort requests through the shared native job lifecycle
  abort owner so a consumed abort clears active-job state, file identity, and
  timing, and resets the consumed abort latch instead of leaving stale print
  status or stop state behind.
- [x] Route latched native abort requests through the service-level abort owner
  before job streaming so internal abort latches use the same cleanup-before-
  idle callback boundary as explicit `ABORT` commands.
- [x] Clear stale native abort latches when job polling observes no active job
  and when the service context closes, so the native driver cannot keep stop
  state alive after the stream is gone.
- [x] Clear native heater-wait/preheat state on abort, stream failure, finish
  failure, completion, idle poll cleanup, and service close so preheat status
  cannot reappear after a terminal job transition.
- [x] Make old-Marlin ProtoError sequence desync deterministic in native host
  tests: idle and abort/finish cleanup phases resync to the firmware's expected
  sequence, while active print desync clears in-flight state and enters the
  abort cleanup path instead of continuing to stream print moves or leaving the
  service stuck in `printing`.
- [x] Keep native job-streamer motion output tied to the service-owned serial
  readiness flag by reference, matching the motion-runtime adapter and avoiding
  stale copied readiness state if motion transport opens or closes around job
  polling.
- [x] Move native runtime diagnostic counter projection into
  `printsvc/src/runtime_diagnostics.*` so flow in-flight/sent/ACK/resend/reject,
  queued job depth, streamed line number, and planner-starvation fields are not
  refreshed by ad hoc service code, with host tests covering active and idle
  job projections.
- [x] Move native stock-compatible command reply formatting into
  `printsvc/src/command_reply.*` so the `{"status","message"}` response shape
  and JSON escaping are not private service-dispatcher details, with host tests
  for OK, error, escaping, and truncation failure.
- [x] Move native command-audit ownership into
  `printsvc/src/command_audit.*` so command latency clamping, post-command
  diagnostic projection, and command/status log emission wrap dispatch through
  one tested boundary instead of inline service-shell code.
- [x] Keep well-formed but unknown native command frames on the audited dispatch
  path, so unsupported verbs receive the normal `unknown command` reply and
  command diagnostics instead of being flattened into malformed-frame handling.
- [x] Move native IPC command-frame handling into a named helper with host tests
  so valid commands, audited unknown verbs, and malformed-frame `bad command`
  replies are not embedded directly in the ZMQ service loop.
- [x] Route the built-in native local-job smoke path through the same IPC
  command-frame helper, so local executable smoke testing does not bypass the
  ZMQ-loop command framing policy it is meant to validate.
- [x] Move native command-dispatch ownership into
  `printsvc/src/command_dispatch.*` so `GCODE`, `MACRO`, `JOB`, `ABORT`,
  `PAUSE`, and `RESUME` routing, command error replies, macro callback wiring,
  job acceptance, and abort policy dispatch are not embedded in the service
  shell, with direct host tests for dry-run G-code and invalid command states.
- [x] Move native raw-G-code command ownership into
  `printsvc/src/gcode_control.*` so multi-line `GCODE` command dispatch,
  packet-send failures, command error mapping, and stock-compatible replies
  are not embedded in generic command routing.
- [x] Make native device startup fail closed when Marlin serial cannot be
  opened. Dry-run packet generation is available only through explicit
  `deneb-printsvc --dry-run` host/lab use and is not used by the packaged init
  script.
- [x] Move REST heat-target parsing and raw heater G-code planning into
  `common/print/gcode_command.*` so web/API temperature endpoints no longer
  locally clamp targets or hand-select `M104`/`M140` command formatting.
- [x] Add a shared native heater-target formatter used by touchscreen
  temperature controls and web/API heat planning so nozzle-vs-bed command
  selection has one owner.
- [x] Move web/API jog and absolute-position request parsing into
  `common/print/gcode_command.*` so REST endpoints no longer own axis,
  distance, coordinate, speed, build-volume, or raw move-command planning.
- [x] Move native pause/resume command ownership into
  `printsvc/src/pause_resume_control.*` so command-level pause/resume replies,
  heater-wait-aware resume routing, active-print pause/resume motion-policy
  queues, and invalid-state errors are not embedded in generic command
  dispatch.
- [x] Move native motion-send ownership into
  `printsvc/src/motion_sender.*` so Marlin packet preparation, serial-ready
  behavior, resend packet writes, and multi-command motion-policy dispatch are
  no longer private `service.c` helpers, with host tests for dry-run sends,
  resend lookup, invalid input, and abort-policy expansion.
- [x] Give native motion-send failures named return codes for invalid input,
  flow-window pressure, and serial transport failure, then map raw `GCODE`
  command send failures to serial faults when the transport write fails.
- [x] Preserve named native motion-send failures through multi-command motion
  policy dispatch and centralize the serial-vs-command error mapping in
  `printsvc/src/motion_send_error.*` so raw G-code, macros, job streaming,
  abort cleanup, and finish cleanup do not duplicate classification logic or
  flatten cleanup-send failures.
- [x] Move native macro/file streaming ownership into
  `printsvc/src/macro_runner.*` so macro path resolution, bounded G-code stream
  iteration, abort checks, flow-window waits, motion sends, and motion polling
  are driven by a tested callback contract instead of private service helper
  loops.
- [x] Preserve native macro-runner callback failure reasons so macro execution
  can distinguish serial transport write and motion-poll failures from generic
  macro/path failures instead of flattening every failed macro line into the
  same error.
- [x] Move native macro-command ownership into
  `printsvc/src/macro_control.*` so macro command execution, flow-window
  waiting, abort checks, motion polling, G-code send callbacks, and command
  error mapping are not embedded in generic command dispatch.
- [x] Move native motion-observation ownership into
  `printsvc/src/motion_observer.*` so Marlin status parsing, heater-wait
  updates, ACK/reject/resend accounting, and resend-sequence detection are one
  tested per-line policy instead of inline serial-poll logic.
- [x] Move native motion-runtime ownership into
  `printsvc/src/motion_runtime.*` so serial open, readiness state, Marlin line
  polling, observer dispatch, resend handoff, and serial close are one tested
  runtime boundary instead of inline service shell logic.
- [x] Make native resend handoff fail visibly when Marlin requests a resend but
  the serial write cannot be performed while transport is marked ready; the
  runtime now enters a serial-error state instead of silently continuing after a
  failed resend attempt, and repeated protocol rejects for the same in-flight
  packet are bounded so stale resend storms fail visibly.
- [x] Move native service-context ownership into
  `printsvc/src/service_context.*` so service initialization, motion-runtime
  adapter wiring, job-streamer adapter wiring, diagnostics projection, and
  close cleanup are one tested boundary instead of repeated service-shell
  plumbing.
- [x] Move native service-command ownership into
  `printsvc/src/service_command.*` so audit wrapping, dispatch handoff, and
  command reply propagation are a tested service-level boundary instead of a
  private service-shell trampoline.
- [x] Publish native diagnostics for flow in-flight depth, sent/ACK/resend/reject
  counters, queued job depth, streamed job line number, command latency, and a
  planner-starvation indicator so lab comparisons have one service-owned source.
- [x] Add a low-volume native diagnostics log module for
  `/var/log/ultimaker/deneb-printsvc.log` with stock-shaped status fields next
  to native phase, stop-allowed, serial ACK/reject/resend, queue depth, job
  line, command latency, and planner-starvation fields, plus host tests for the
  emitted comparison keys.
- [x] Add direct native print-service routing for touchscreen and web/API
  clients: LCD `backend_comm` and web `backend_zmq` select native
  `deneb-printsvc` status `5555` and command `5556` endpoints without a stock
  coordinator or UCI/env override path.
- [x] Move the native print backend route decision into
  `common/print/print_backend_route.*` with host tests so LCD, web/API, and
  `deneb-printsvc` tests share the same native endpoint constants instead of
  duplicating route logic.
- [x] Publish the selected native print backend route
  through shared native route diagnostics: LCD and web/API backend modules now
  expose route accessors, web status JSON includes the selected backend and
  endpoint URLs plus `native_only_route:true`, and host tests cover the shared
  formatter so regular status payloads carry the same native-only proof as the
  dedicated route endpoint.
- [x] Add a Deneb API route diagnostic endpoint,
  `GET /api/v1/deneb/print_backend`, so lab validation can query the selected
  native-printsvc route without parsing the full status
  payload or consulting Python/coordinator state, including a
  `native_only_route` field that proves the stock coordinator route is not
  selectable.
- [x] Move route identity checks behind typed native backend accessors so
  Deneb API route diagnostics, web backend, and LCD backend no longer compare
  presentation strings to decide whether the native `deneb-printsvc` route is
  active.
- [x] Cross-compile and package `deneb-printsvc` into the `.deneb` release
  artifact and make it the Deneb print backend without retaining a stock
  `printserver` route flag.
- [x] Remove the installer-time stock coordinator Python patch from the
  de-Python path: Deneb no longer rewrites
  `/home/cygnus/coordinator/companion/printer_service_command.py` during
  install, leaving stock Python files untouched while native `deneb-printsvc`
  routing remains owned by Deneb C/shell code.
- [x] Remove Deneb-authored Python process launch lines from generated
  printserver/coordinator init shims and drop their Python runtime environment
  exports. Deneb's generated printserver shim no longer delegates back to the
  stock driver, and coordinator backup delegation is kept outside the print
  driver route.
- [x] Move default pending-job JSON-array reads and idempotent pending-file
  cleanup behind `common/print/pending_job_file.*` so Deneb and Cura REST
  endpoints no longer spell the bridge path directly when serving queued jobs,
  and duplicate cleanup paths do not fail if another native caller already
  consumed or cleared the pending metadata.
- [x] Add a packaged no-Python live smoke/resource harness,
  `deneb-printsvc-smoke`, for supervised lab validation of native route
  selection, boot/backend readiness, idle status snapshots, heat/cool, Z-home,
  observe-only client API/bridge surfaces, macro-backed manual actions,
  optional multipart job upload, pause/resume,
  abort, explicit native local/USB job acceptance, explicit preheat abort,
  explicit active-print abort, Cura cluster API job upload/abort, short-job
  completion, native service-restart
  recovery, process/resource samples, and native-route assertion. The harness
  installs with Deneb but only observes unless a tester explicitly enables
  native route, boot-sync, heat, motion, macro, local-job, REST job,
  client-proof, preheat-abort, active-abort with a configurable active-print
  delay, Cura job, complete-job, or restart phases, and writes both
  a full log plus a compact summary with phase results, bounded boot-sync
  ready timing, scalar `/printer/status` values plus sanitized full status
  bodies, `/printer` root snapshots with Deneb-native active/stop-allowed
  flags, and `/proc`-sourced process RSS/VSZ samples, CPU
  jiffies, load averages, uptime samples, and completed-job throughput records,
  including native route/status snapshots after native-route tests. Completion
  waits default to a bounded 300 seconds and timeout with current status/root
  evidence plus a best-effort API abort, so a live completion safety blocker
  produces actionable summary evidence instead of an unbounded smoke hang. The
  harness also rejects dwell/M400-only completion fixtures before upload, so
  completion evidence must use a bounded fixture with real progress commands
  instead of a firmware wait file or a temperature-poll loop. The same harness
  can generate a bounded old-Marlin Z movement fixture with
  `--make-complete-fixture`: it homes Z once, performs up to 480 small relative
  `G1 Z-0.20 F30` moves away from homed Z max, avoids heat/extrusion/dwell/newer
  commands and avoids moving farther into the Z max endstop, with total travel
  capped to 96 mm away from homed Z max. Generated job fixtures now include an
  early `G280 S1` stock-prime marker; stock `print_service.py` treats that as
  an existing prime command and avoids adding its cold-extrusion startup prime,
  while native rewrites it to a non-extruding `G92 E-16.5`. The harness now
  also generates a bounded Z-only active-job fixture with
  `--make-active-fixture` and a low-temperature heater-wait fixture with
  `--make-preheat-abort-fixture`, so active-abort, Cura-upload, pause/resume,
  and preheat-abort smoke runs can use fresh, known fixtures instead of stale
  printer-side test files. The harness
  also generates a bounded Cura-style XYZ fixture with
  `--make-representative-fixture`; generated representative fixtures contain no
  heat, material extrusion, dwell, or internal homing commands and force
  `--prehome-action home` before any smoke phase may upload them. The shell
  selftest covers those generators plus the dwell-only rejection path, and the
  native audit rejects packages missing the safe fixture generators or the
  representative all-axis-home gate. This adds safer representative-geometry
  test input, not live representative Cura proof by itself.
- [x] Add observe-only client proof to the live smoke harness: `--client-proof`
  samples the native Deneb print-backend diagnostic, UM API `/printer/status`,
  `/printer`, `/system`, Cura cluster `/printers`, `/print_jobs`, `/materials`,
  and installed `deneb-df-bridge status` without heat or motion. Optional UM
  `/print_job` probing is recorded as `optional_phase` so an absent legacy
  endpoint remains visible without satisfying or poisoning the required client
  proof. The verifier's `--client-proof` mode requires the native-only route,
  idle native active/Stop flags false, Cura cluster responses, Digital Factory
  bridge status, and a successful client-proof completion row.
- [x] Deploy `Deneb_Update_be6a5b7` and collect first generated-fixture physical
  abort evidence on native `deneb-printsvc`: installed selftests passed, on-device
  active/preheat fixture generation passed, observe-only native restart/boot-sync
  passed, low-temperature preheat-abort smoke verified Stop allowed during
  preparation and final idle, and bounded Z-only active-abort smoke verified
  Stop allowed during printing and final idle. This evidence is deliberately
  narrow and does not close representative Cura, pause/resume, completion, or
  stock/native resource gates.
- [x] Deploy `Deneb_Update_7f070d7` and collect bounded Z-only completion
  evidence on native `deneb-printsvc`: host CTest passed, the package was
  installed over SSH, `/tmp/deneb-complete-ui-label-fix.gcode` ran through
  `/usr/bin/deneb-printsvc-smoke --physical-ok --native --complete-job`, and
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --complete-job`
  accepted the summary. The run proved native-only route, no stock
  `print_service.py`, initial/final idle with native active and Stop disabled,
  a Z-home physical safety plan, and completion logs from both `deneb-api` and
  `deneb-ui` without a filtered `serial_fault` or flow-control desync. This
  closes the bounded completion regression only; it does not close
  representative Cura/slicer completion, pause/resume, or stock/native
  resource gates.
- [x] Add a packaged shell-only verifier,
  `deneb-printsvc-smoke-verify`, so future live summary files can be checked
  for observe-only, native-route, explicit idle status, boot-sync readiness,
  client API/bridge proof,
  heat/cool, Z-home,
  macro-backed action, local/USB native job-start/abort, REST job-start/abort,
  explicit preheat abort, explicit active-print abort, Cura cluster job-start/abort, pause/resume, short-job completion, native
  service-restart, and native-route evidence without Python or ad hoc log
  inspection. Native-route evidence now requires the route diagnostic body and
  captured status bodies to report native-only route evidence while keeping
  the summary `status=` field as a scalar UI state
  (`print_backend:native` plus `native_only_route:true` on route diagnostics,
  and `native_only_route:true` on status bodies), `deneb-printsvc` to be
  running with no stock `print_service.py` process, and no captured native
  process sample to contain `print_service.py`; local/USB job evidence now
  requires that same native ownership proof plus native CLI-emitted accepted
  `pre_print` active/stop-allowed state and aborted `idle` inactive/stop
  disabled state. The verifier also checks
  active-job UI status
  transitions: `printing` while active or active-abort evidence is being
  captured, `paused` after pause, and `idle` after abort or natural completion,
  including the active `printing` status snapshot before a short completion
  test is allowed to settle to idle. Active, preheat,
  active-abort, paused, resumed, aborted, and completed snapshots must also prove native
  active/stop-allowed flags so the LCD Stop safety state is checked from the
  native driver contract. Idle verification now requires the initial status to
  be `idle` with initial native active/stop-allowed flags false. Heat,
  motion, and macro phases must include their snapshot status/root API samples
  so command acceptance alone cannot satisfy those live checks. Its
  resource mode requires initial/final memory, uptime, CPU, load, process RSS,
  native `deneb-printsvc` driver RSS when native mode is required, and
  completed-job throughput evidence so future stock/native live runs can
  satisfy the before/after measurement gate. It also has an explicit `--stock`
  assertion for baseline collection that fails unless the stock
  `/home/cygnus/marlindriver/print_service.py` process owns the route and
  `/usr/bin/deneb-printsvc` is absent, so stock measurements cannot be
  accidentally taken against the native driver.
- [x] Add a packaged shell-only comparator,
  `deneb-printsvc-smoke-compare`, so stock and native live summary files can be
  compared on-device without Python. It emits before/after deltas for system
  memory, driver-process RSS, raw CPU jiffies, initial-to-final CPU jiffies,
  boot-sync elapsed
  time, and print throughput, and fails if the stock summary lacks
  an explicit stock process ownership row, lacks initial/final
  `print_service.py` process evidence, contains a native `deneb-printsvc`
  process sample, has CPU or throughput intervals that are not positive, or the
  native summary lacks per-lifecycle status
  values with native-only route evidence, lacks scalar boot-sync status plus a
  native-only boot-sync status body, contains any stock `print_service.py`
  process sample, lacks native local/USB IPC job acceptance, accepted
  stop-state, abort, and idle-state evidence, lacks active-print abort evidence, lacks
  `deneb-printsvc` process ownership, or lacks per-lifecycle native
  active/stop-allowed safety evidence for each required active and inactive
  printer-root snapshot. Its `--require-reduction` mode turns the before/after
  resource gate into a failing check by requiring native system memory,
  driver-process RSS, CPU interval, and boot-sync elapsed time to be lower than
  stock while keeping native print throughput at least stock.
- [x] Add a shell-only synthetic selftest,
  `deneb-printsvc-smoke-selftest`, that builds stock/native summary fixtures
  and runs the full smoke verifier plus stock/native comparator without Python.
  It also invokes the live smoke harness' no-hardware
  `--summary-parser-selftest` mode so scalar status extraction from JSON,
  flat, and raw status bodies is covered by the packaged shell gate.
  The selftest also checks expected-failure fixtures for missing native
  stop-allowed evidence, single missing active and inactive comparator
  stop-safety phases, missing native local/USB job evidence, a route diagnostic
  that is not native-only, a stock `print_service.py` process returning in a
  native run, missing stock
  `print_service.py` baseline evidence, a stock baseline captured while native
  `deneb-printsvc` is still present, missing native `deneb-printsvc` RSS
  evidence, zero-throughput records, and nonzero
  throughput regressions under strict reduction mode. This does
  not replace live hardware evidence, but it prevents the packaged
  verifier/comparator gates from drifting away from the Section 8 smoke and
  resource requirements, including rejection of summaries where status
  snapshots lose `native_only_route:true` in either verifier or comparator
  paths, including a single missing comparator lifecycle status snapshot,
  missing natural-completion active status evidence, reporting the wrong
  lifecycle status during initial idle, preheat/abort, or active-abort evidence, putting the full status body
  into boot-sync `status=`, or omitting boot-sync `status_body` native-route
  proof.
  It also now rejects missing generic-job and Cura-cluster
  abort-requested/draining status evidence, so full-summary evidence cannot
  skip the Stop-disabled abort cleanup window that caused earlier LCD/API
  regressions.
- [x] Capture observe-only client API/bridge evidence after package install: on
  June 9, 2026, `dist/Deneb_Update_fa29a67.deneb` installed over SSH and the
  installed `/usr/bin/deneb-printsvc-smoke --native --boot-sync --client-proof`
  summary passed `/usr/bin/deneb-printsvc-smoke-verify --native --idle
  --boot-sync --client-proof`. The run proved native-only route, boot-sync
  readiness, UM API idle status/root/system responses,
  `native_active:false`, `native_stop_allowed:false`, Cura cluster
  `/printers`, `/print_jobs`, and `/materials` responses, installed
  Digital Factory bridge status (`status_timeout` accepted while offline), no
  stock `print_service.py`, live ambient temperatures around 30.3 C bed and
  32.8 C nozzle, and process RSS samples for `/usr/bin/deneb-printsvc`,
  `/usr/bin/deneb-api`, and `/usr/bin/deneb-ui`. This closes only the
  observe-only client-surface proof; it does not close LCD/Web workflows, Cura
  upload/start/abort, Digital Factory job lifecycle, heat/motion, or
  stock/native resource comparison.
- [x] Add a shell-only native binary CLI selftest,
  `deneb-printsvc-cli-selftest`, that runs the real `deneb-printsvc`
  `--smoke-test` and `--local-job-smoke` entry points against a temp G-code
  file without opening the motion serial device, then requires the native
  local-job accepted and aborted stop-state rows.
- [x] Add a shell-only native init handoff selftest,
  `deneb-printsvc-init-selftest`, that checks the packaged
  `deneb-printsvc.init`, installer source, installer-generated printserver
  heredoc, and installed/generated printserver shim for the native ownership
  marker, exact stock `/home/cygnus/marlindriver/print_service.py` cleanup,
  stale `/var/run/printserver.pid` cleanup, correct startup/stop cleanup
  ordering, and absence of Python driver launch commands.
- [x] Add a shell-only native route/package audit,
  `deneb-printsvc-native-audit`, that verifies Deneb source clients still
  default to the native print-service route, no stock print backend selector has
  returned, no Deneb runtime launches stock `print_service.py`, and unpacked or
  archived update packages include the native print-service tools while
  rejecting Python driver artifact names.
- [x] Add a shell-only native audit negative-fixture selftest,
  `deneb-printsvc-native-audit-selftest`, that proves the audit fails closed
  when source reintroduces a stock print backend selector, source launches the
  stock Python print driver, package-builder audit wiring is removed, an
  installer audit-selftest handoff is removed, installer web-service restart
  wiring is removed, an unpacked package lacks the audit tool, an unpacked
  package contains a Python driver artifact name, an
  unpacked package omits the native-printsvc release gate, or when an archived
  package contains a Python driver artifact name.
- [x] Extend the shell-only integration-audit selftest so source fixtures fail
  when touchscreen or web/API client adapters embed raw motion/heater G-code
  literals or direct pending-job path references instead of using shared
  print-control helpers.
- [x] Verify the `.deneb` release package includes the native smoke harness,
  `deneb-printsvc`, and its declared notices: local package build
  contains `deneb-printsvc`,
  `deneb-printsvc-smoke`, `deneb-printsvc-smoke-verify`,
  `deneb-printsvc-smoke-compare`, `deneb-printsvc-smoke-selftest`,
  `deneb-printsvc-stock-baseline`, `deneb-printsvc-cli-selftest`,
  `deneb-printsvc-init-selftest`,
  `deneb-printsvc-release-gate-selftest`, `deneb-printsvc-native-audit`,
  `deneb-printsvc-native-audit-selftest`, `deneb-printsvc-macros/`,
  `manifest.txt`, and `LVGL_LICENSE_TLSF.txt`, with
  no packaged Python or `print_service.py` entries. The package builder now
  fails if a Python driver artifact is present in the staging directory or the
  final `.deneb` archive, and `tools/build-update-release.ps1` also inspects the
  produced `.deneb` archive so release automation fails closed if a Python
  driver artifact is packaged or `deneb-printsvc` /
  `deneb-printsvc-smoke-selftest` / `deneb-printsvc-cli-selftest` /
  `deneb-printsvc-init-selftest` / `deneb-printsvc-release-gate-selftest` /
  `deneb-printsvc-native-audit` /
  `deneb-printsvc-native-audit-selftest` is missing.
  The package now also ships `deneb-printsvc-stock-baseline`, a guarded
  shell-only stock-baseline collector that requires an explicit stock-switch
  acknowledgement, starts the backed-up stock printserver or
  `/rom/etc/init.d/printserver`, runs `deneb-printsvc-smoke --stock`, and
  restores native `deneb-printsvc` in a trap. The helper deletes the active
  native procd service and keeps a scoped guard during the stock smoke window so
  delayed native respawn cannot contaminate stock process evidence. The native
  audit and negative-fixture selftest reject packages or source trees that omit
  that helper or remove its acknowledgement/restore hooks.
  The native route/package audit also rejects unpacked packages that omit the
  smoke verifier selftest, native CLI selftest, init-handoff selftest, native
  audit selftest, or declared `LVGL_LICENSE_TLSF.txt` notice, so the package
  evidence contract is checked by an independent shell gate as well as by the
  release wrapper. The same static audit now checks that the shell package
  builder defaults native print-service packages to `experimental`, gates
  `nightly`/`stable` on stock and native live summaries, runs
  `deneb-printsvc-smoke-verify --stock --resources`,
  `deneb-printsvc-smoke-verify --full`, runs
  `deneb-printsvc-smoke-compare --require-reduction`, and that the PowerShell
  release wrapper exposes and preflights the same live-summary boundary.
  `deneb-printsvc-release-gate-selftest` also behaviorally exercises invalid
  channels, stable builds without summaries, and nightly builds with missing
  stock or native summary files or a malformed native summary, so those
  fail-fast paths are covered before a package can be accepted. The selftest
  uses an isolated
  `DENEB_PACKAGE_VERSION_OVERRIDE` staging name and cleans that staging/output
  after the expected failures so it cannot clobber the active package build.
  Both `ui/build-package.sh` and the PowerShell release verifier require the
  packaged smoke, CLI, init, release-gate, and native audit tools to be present
  while running the shell-only smoke/release-gate/init/native-audit gates that
  do not execute the
  cross-compiled target binary. The print-service CTest suite runs the shell
  smoke, native CLI, init, release-gate, route/package audit, and
  negative-fixture audit selftests against the host build, and the installer
  deploys the release-gate evidence tool to
  `/usr/bin/` beside the live smoke verifier and comparator. The installer also
  rejects update packages that contain Python driver artifacts, runs the
  packaged native audit over the unpacked update, and runs the installed
  print-service smoke-tool, CLI, native-audit, and init-handoff selftests before
  completing the update. The installed `/usr/bin/deneb-printsvc-init-selftest`
  now falls back to `/etc/init.d/deneb-printsvc` and `/etc/init.d/printserver`
  when package sources are absent, and
  `/usr/bin/deneb-printsvc-release-gate-selftest` validates the installed
  manifest/toolchain instead of requiring `ui/build-package.sh` on target.
- [x] Capture observe-only live native route evidence after package install:
  on June 9, 2026, package `dist/Deneb_Update_8816c0b.deneb` installed over SSH
  and rebooted cleanly. The installed CLI, init-handoff, release-gate, and
  native-audit selftests passed under `set -e`; `ps` showed
  `/usr/bin/deneb-printsvc` with no `print_service.py`; and
  `/usr/bin/deneb-printsvc-smoke --native --restart --boot-sync` verified
  native-only route, boot-sync readiness, idle status, service restart recovery,
  `native_active:false`, `native_stop_allowed:false`, ambient bed/nozzle
  readings around 25.5 C / 28.3 C, and `print_service_py=0`. This was
  observe-only evidence; heat, motion, Cura job, pause/resume, completion, and
  active/preheat abort physical phases remain open.
- [x] Deploy `Deneb_Update_f83c1a1` and refresh installed native ownership plus
  observe-only client proof after the pause-position safety fix: on June 9,
  2026, the package installed over SSH, `/etc/deneb/manifest.txt` reported
  `version: f83c1a1`, and installed CLI, init-handoff, release-gate,
  native-audit, integration-audit, and integration-audit selftests passed. The
  installed `/usr/bin/deneb-printsvc-smoke --native --boot-sync
  --client-proof` summary passed `/usr/bin/deneb-printsvc-smoke-verify
  --native --idle --boot-sync --client-proof`, proving native-only route, idle
  native active/Stop flags false, no stock `print_service.py`, UM API
  `/printer/status`, `/printer`, and `/system`, Cura cluster `/printers`,
  `/print_jobs`, and `/materials`, installed Digital Factory bridge status,
  nonzero ambient telemetry around 29.7 C bed and 32.7 C nozzle, and final
  `/usr/bin/deneb-printsvc` RSS around 1576 KB. This refreshes the non-motion
  client-surface proof for the current build; it does not close the supervised
  all-axis pause/resume motion proof, representative Cura geometry, or
  stock/native resource comparison gates.
- [x] Deploy `Deneb_Update_af12aaf` and refresh installed observe-only proof
  after moving LCD abort-display context into shared status-state helpers: on
  June 9, 2026, the package installed over SSH, `/etc/deneb/manifest.txt`
  reported `version: af12aaf`, installer smoke/CLI/native-audit selftests
  passed, and the installed `/usr/bin/deneb-printsvc-smoke --native
  --boot-sync --client-proof` summary passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --boot-sync
  --client-proof`. The run proved native-only route, no stock
  `print_service.py`, idle native active/Stop flags false, UM API and Cura
  cluster client surfaces, installed Digital Factory bridge status, nonzero
  ambient telemetry around 29.9 C bed and 32.8 C nozzle, and final
  `/usr/bin/deneb-printsvc` RSS around 1584 KB. This is non-motion evidence for
  the current shared-helper build; hardware LCD abort-flow, representative
  Cura geometry, and strict stock/native resource comparison remain open.
- [x] Deploy `Deneb_Update_b0fa27b` and refresh current native ownership,
  client/firmware proof, target selftests, and bounded completion/resource
  evidence after sharing Cura cluster action routing. On June 10, 2026, the
  target manifest reported `version: b0fa27b`; installed CLI, init-handoff,
  release-gate, native-audit, and integration-audit selftests passed; `ps`
  showed `/usr/bin/deneb-printsvc` and no stock `print_service.py`; and
  observe-only `/usr/bin/deneb-printsvc-smoke --native --restart --boot-sync
  --client-proof --firmware-proof` passed target verification with ambient
  bed/nozzle/topcap telemetry and final idle active/Stop flags false. A
  supervised bounded Z-only completion run then passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --complete-job
  --resources`, proving active printing status/Stop allowance during the job,
  final idle active/Stop false, and native-side RSS/throughput samples. This
  refreshes current native proof only; representative Cura geometry, LCD/Web UI
  user flows, and strict stock/native resource comparison remain open.
- [x] Capture paired observe-only stock/native firmware and ambient telemetry
  evidence for the current native-printsvc build. On June 10, 2026, the
  `d82245c` stock-baseline helper ran against the backed-up stock `printserver`
  and `/tmp/deneb-stock-d82245c.summary` passed
  `/usr/bin/deneb-printsvc-smoke-verify --stock --firmware-proof`: stock
  `print_service.py` owned the window, native `deneb-printsvc` was absent from
  the stock summary, bed/nozzle/topcap ambient telemetry was nonzero, and native
  was restored afterward. The paired native
  `/tmp/deneb-native-d82245c-observe.summary` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --boot-sync
  --client-proof --firmware-proof` with native ownership, no stock
  `print_service.py`, UM API/Cura cluster/Digital Factory status proof, and
  final idle active/Stop flags false. This validates observe-only
  firmware/temperature parity collection; strict stock/native `--resources`
  comparison, representative Cura/slicer geometry, LCD/Web UI user flows, and
  Digital Factory job lifecycle remain open.
- [x] Deploy the representative-fixture harness update and validate the
  no-motion target safety gate. On June 10, 2026, the updated
  `Deneb_Update_d82245c.deneb` installed on the printer, the installed smoke
  harness generated `/tmp/deneb-representative-xyz.gcode` with bounded
  Cura-style XYZ moves, no heat, no extrusion, no dwell, and the
  `DENEB_REPRESENTATIVE_XYZ_FIXTURE=1` marker, then rejected
  `--job /tmp/deneb-representative-xyz.gcode` without `--prehome-action home`
  before upload with `phase=representative-fixture-safety-gate rc=2
  reason=requires_all_axis_home`. This proves the target-side safety tooling
  for representative geometry input, not the physical representative Cura run.
- [x] Make abort evidence wait for true native idle instead of fixed sleeps.
  During the June 10, 2026 representative Cura smoke, the native service
  settled from `ABORT` to `Idle` about one second after the old fixed
  `cura-job-aborted` snapshot was captured. The smoke harness now keeps the
  abort-requested/draining snapshots but polls `/printer/status` and
  `/printer` until `idle`, `native_active:false`, and
  `native_stop_allowed:false` before recording final `*-aborted` evidence, with
  `DENEB_PRINTSVC_SMOKE_ABORT_SETTLE_TIMEOUT` / `--abort-settle-timeout` as the
  bounded timeout.
- [x] Capture supervised representative Cura-cluster abort evidence on hardware.
  After redeploying the abort-settle harness on June 10, 2026,
  `/usr/bin/deneb-printsvc-smoke --physical-ok --native --cura-job
  /tmp/deneb-representative-xyz.gcode --prehome-action home
  --abort-settle-timeout 90` passed against the generated bounded
  representative XYZ fixture. The run proved native ownership, all-axis
  prehome, Cura cluster upload/start, active `printing` with
  `native_active:true` / `native_stop_allowed:true`, cluster DELETE abort,
  immediate and draining `aborting` with Stop disabled, `cura-job-aborted-wait
  elapsed=10 rc=0 status=idle`, final `idle` with native active/Stop flags
  false, ambient bed/nozzle targets still 0, and no pending Cura jobs. The
  installed verifier passed with
  `/usr/bin/deneb-printsvc-smoke-verify --native --cura-job
  /tmp/deneb-cura-representative-xyz.summary`. This closes generated
  representative Cura-cluster evidence; desktop Cura client behavior and
  real slicer-output parity remain separate follow-up proof.
- [x] Refresh heat, Stop-action abort, and local/USB native evidence after the
  REST temperature-target compatibility fix. On June 10, 2026, the installed
  `Deneb_Update_d82245c.deneb` package was rebuilt/redeployed and
  `/usr/bin/deneb-printsvc-smoke --physical-ok --native --heat` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --heat` on
  `/tmp/deneb-native-heat-fixed2.summary`: bed/nozzle targets reached
  40 C / 50 C, `/printer/status` reported `printing`, `/printer` reported
  `native_active:true` / `native_stop_allowed:true` during heat, and cooldown
  returned to `idle` with native active/Stop flags false. The representative
  Stop-action path also passed
  `/usr/bin/deneb-printsvc-smoke --physical-ok --native --active-abort
  /tmp/deneb-representative-fixed.gcode --active-abort-delay 5
  --prehome-action home` plus `--native --active-abort` verification, proving
  all-axis prehome, active `printing` with Stop allowed, API abort
  `aborting` with Stop disabled, and final `idle` with native active/Stop flags
  false. Finally, `/usr/bin/deneb-printsvc-smoke --physical-ok --native
  --local-job /tmp/deneb-representative-fixed.gcode --prehome-action home`
  passed `--native --local-job`, proving native local/USB acceptance enters
  stoppable `pre_print`, aborts, and settles idle. These runs close current
  generated heat/Stop-action/local-job regressions; they do not replace desktop
  Cura, LCD/Web UI hands-on, Digital Factory lifecycle, or strict stock/native
  resource proof.
- [x] Gate non-experimental native print-service packages on live evidence:
  `ui/build-package.sh` defaults to `DENEB_RELEASE_CHANNEL=experimental` and
  refuses `nightly` or `stable` native-printsvc packages unless
  `DENEB_PRINTSVC_STOCK_SUMMARY` and `DENEB_PRINTSVC_NATIVE_SUMMARY` point at
  captured stock/native smoke summaries. The builder verifies the stock
  summary with `deneb-printsvc-smoke-verify --stock --resources`, verifies the
  native summary with `deneb-printsvc-smoke-verify --full`, and then runs
  `deneb-printsvc-smoke-compare --require-reduction` before creating the
  archive. `tools/build-update-release.ps1` exposes the same boundary through
  `-ReleaseChannel`, `-PrintsvcStockSummary`, and
  `-PrintsvcNativeSummary`, failing before build work starts when a
  non-experimental channel lacks both live summaries, while the package
  manifest records the experimental native-printsvc status and the required
  non-experimental evidence gate. Both the shell package builder and
  PowerShell release wrapper inspect the archived manifest for that channel and
  native-printsvc gate before accepting the artifact, and the installer now
  rejects packages whose manifest omits the native-printsvc experimental status
  or evidence gate, preserves accepted manifests at `/etc/deneb/manifest.txt`,
  and statically selftests that handoff in
  `tools/deneb-printsvc-init-selftest.sh`.
- [x] Tighten native completion/resource proof around real Marlin flow drain.
  The June 10 `d82245c` EOF-drain fix keeps native jobs active when the G-code
  stream reaches EOF but flow-control packets are still in flight, then starts
  finish cleanup only after the in-flight queue drains. Host tests now cover
  that EOF-with-inflight case, and the smoke verifier/comparator require
  completed native resource summaries to show `flow_inflight:0` and
  `flow_resend:0`. Two attempted optimizations were rejected on hardware:
  increasing the stream window from 4 to 6 produced
  `/tmp/deneb-native-resources-window6.summary` with resend debt and partial Z
  completion, and reducing the finish-drain delay produced
  `/tmp/deneb-native-resources-fastfinish.summary` with premature idle at the
  wrong Z height. The
  conservative stream window 4 and finish drain 8/3 remain intentional until a
  safer throughput fix is proven.
- [x] Restore stock-matched native completion throughput without widening the
  Marlin flow window. The follow-up dirty `cd5eeba` build keeps stream window
  4, uses a faster active job cadence with throttled status publishing, excludes
  idle telemetry flow debt from active cadence, and is indexed in
  [docs/PRINTSVC_EVIDENCE_LEDGER.md](docs/PRINTSVC_EVIDENCE_LEDGER.md).
- [x] Match stock Python's Marlin sequence-number ring in native flow control.
  The stock `MarlinProtocolNode.send()` increments sequence numbers through
  254 and then wraps directly to 0; it never transmits sequence 255. Native
  flow control now uses the same 0..254 ring and acknowledges older in-flight
  packets across the 254-to-0 boundary. Host coverage sends 260 commands
  without emitting 255 and verifies an `o00...` ACK drains 253, 254, and 0
  while leaving newer sequence 1 in flight. This targets the long-job
  sequence-wrap/resend instability seen during completion/resource runs.
  After redeploying the dirty `Deneb_Update_560025d.deneb` harness build, the
  native rerun `/tmp/deneb-native-g280-resource-v2.summary` passed
  `--native --complete-job --resources`: `running_z=207.0`,
  `final_z=111.0`, `delta_z=96.000`, `phase=printer-job-completed-flow-wait`
  showed idle `flow_inflight=0`/`flow_resend=0`, and native driver RSS stayed
  around 1.1 MB. This confirmed the previous native `flow_inflight:2` failure
  was a smoke snapshot timing issue caused by normal idle `M105`/`M114`
  telemetry, not unfinished job flow. The fresh guarded stock baseline
  `/tmp/deneb-stock-g280-resource-v2.summary` is still invalid release
  evidence: the stock log shows `Command 'b'JOB'' not supported when busy`
  immediately after the harness pre-homed Z, because stock status looked idle
  before the stock driver had left its internal busy state. The summary then
  reported 318 B/s but was already idle at the delayed running snapshot and
  stayed at `running_z=207.0`, `final_z=207.0`, `delta_z=0.000`. The smoke
  harness now waits for stock Z-home position plus a fixed stock prehome
  settle window before uploading the completion job. The strict stock/native
  resource gate remains open until that updated stock-baseline route proves the
  bounded descent body executes under stock Python.

## 9. Motion Controller / Marlin Firmware

- [ ] Inventory the bundled motion-controller firmware hex and identify its upstream base/version.
- [ ] Locate or reconstruct the corresponding source for the current UltiMaker-modified Marlin firmware before changing it.
- [ ] Catalog UltiMaker-specific G-code commands and behavior used by Cura, Cygnus, printserver, and material/bed-leveling workflows.
- [ ] Compare current firmware behavior against current Marlin 2.0.x behavior.
- [ ] Decide whether to port UltiMaker changes forward into latest Marlin 2.0.x or backport selected modern fixes into the existing base.
- [ ] Maintain compatibility with existing ecosystem-specific commands required by Cura and printer services.
- [ ] Investigate current pause/resume behavior. The current Marlin path may not support native pause/resume semantics cleanly.
- [ ] Define desired pause/resume behavior across Marlin, printserver, touchscreen UI, web UI, and Cura LAN API.
- [ ] Add tests for pause, resume, abort, power/loss-like interruptions, heaters, homing, material moves, bed leveling, and print completion.
- [ ] Treat motion firmware changes as high risk. Validate thermals, endstops, motor directions, acceleration, jerk/junction settings, extrusion, and safety cutoffs.
- [ ] Do not redistribute a modified binary without satisfying the license obligations for the motion firmware source.

## 10. Open Research Items

- [ ] Confirm exact hardware specs from device or authoritative docs: CPU, RAM, flash, local SD/eMMC/storage layout, and available free space.
- [ ] Confirm practical resource ceilings under load: usable free RAM, CPU headroom while printing, flash write endurance concerns, and local storage throughput.
- [ ] Confirm whether this specific printer has camera hardware or only S-series models do.
- [ ] Confirm whether installed firmware version still has non-blocking update signature behavior.
- [ ] Confirm exact service startup order and restart dependencies before installing new services.
- [ ] Confirm whether mDNS support exists already through `procd` or needs a new lightweight advertiser.
- [ ] Confirm whether adding packages is practical within flash/storage constraints.
- [ ] Confirm whether Python 3.6 runtime is acceptable for new services or whether to ship static/native components.

## 11. Milestones

- [x] Milestone 0: public repo scaffold with legal boundary, no vendor files, `.gitignore`, and documentation.
- [x] Milestone 1: SSH-only bootstrap `.img` plan, build, install, and login verification.
- [x] Milestone 2: live-device resource baseline and first resource reduction targets gathered over SSH.
- [ ] Milestone 3: general Deneb USB `.deneb` installer framework with backup, manifest, rollback, and low-memory install behavior.
- [x] Milestone 4: replacement touchscreen UI architecture choice and resource proof. (LVGL v9 C, 2.0MB binary)
- [x] Milestone 5: replacement touchscreen UI MVP preserving core stock functionality. (20-screen catalog, ZMQ IPC, 7 locales)
- [x] Milestone 6: status-only web UI using existing firmware status channels.
- [x] Milestone 7: web UI controls for pause/resume/cancel/manual heat with safety gates.
- [ ] Milestone 8: local storage print-flow verification and USB-removal-safe behavior.
- [ ] Milestone 9: Cura discovery/status compatibility. (`deneb-mdns`, cluster status endpoints, and Cura plugin exist; current Cura/hardware validation remains pending)
- [ ] Milestone 10: Cura LAN upload/start print compatibility. (Upload/start/action paths exist; free-space, storage safety, and real Cura/hardware validation remain pending)
- [ ] Milestone 11: generic slicer G-code support with thumbnails.
- [ ] Milestone 12: print-quality degradation root-cause fix.
- [ ] Milestone 13: de-python `marlindriver` with native `deneb-printsvc` experimental replacement. Native route, package, archive, init handoff, synthetic verifier gates, bounded native physical smoke, and paired observe-only stock/native firmware proof exist; strict stock/native resource evidence and broader client-flow proof remain required. See [Section 8](#8-de-python-marlindriver--native-print-service) and [docs/RESOURCE_REDUCTION_PLAN.md](docs/RESOURCE_REDUCTION_PLAN.md#next-measurement-targets).
- [ ] Milestone 14: Marlin modernization feasibility branch.
- [ ] Milestone 15: OS/service inventory, OpenWrt-specific security review, and resource-reduction plan.
- [ ] Milestone 16: targeted OS/service updates or replacements with rollback.
- [ ] Milestone 17: release package builder exists; signed Stable/Nightly GitHub artifact automation remains pending.
- [ ] Milestone 18: in-device update checker pointed at our GitHub Releases.
- [ ] Milestone 19: boot-time profiling and startup progress screen.

## 12. Do Not Do

- [ ] Do not publish a full modified `omega2.bin` or full root filesystem.
- [ ] Do not publish recovered/decompiled UltiMaker application source without a license permitting it.
- [ ] Do not bypass safety checks for convenience.
- [ ] Do not expose unauthenticated network print/heat/motion controls on untrusted networks.
- [ ] Do not ship features that merely preserve the current overloaded RAM/CPU profile unless they are temporary scaffolding for a measured reduction.
- [ ] Do not ship features that make idle CPU, printing CPU, memory pressure, boot time, or UI latency worse without a documented exception.
- [ ] Do not replace motion firmware until we have source, hardware mapping, safety tests, and a recovery method.
- [ ] Do not claim Cura compatibility until tested against current Cura source and actual Cura behavior.
