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
this section is the milestone checklist for that same work.

Current native replacement progress lives under [printsvc/](printsvc/). The
first buildable slice now provides a lab-gated `deneb-printsvc` binary with
separate modules for ZMQ IPC, command parsing, status serialization, Marlin
status parsing, serial transport, packet/CRC helpers, flow control,
G-code streaming, heater waits, macro lookup, and service state. It is
packaged into `.deneb` releases but remains disabled by default through
`deneb.printsvc.enabled=0` until full integration migration, side-by-side
diagnostics, resource measurements, and live-device validation items are
finished.

- [x] Treat `marlindriver` replacement as a dedicated milestone, not an opportunistic bug fix.
- [ ] Build an original native `deneb-printsvc` replacement for `/home/cygnus/marlindriver/print_service.py`.
- [x] Preserve the stock print-service IPC contract first: raw ZMQ status PUB on `127.0.0.1:5555`, command REP on `127.0.0.1:5556`, topic `10001`, and `COMMAND<json>` request framing.
- [ ] Keep coordinator, LCD UI, web UI, Cura LAN flow, and Digital Factory assumptions compatible during the first replacement stage.
- [ ] Audit every Deneb integration that currently patches around the stock Python driver: LCD `backend_comm`, web `backend_zmq`, `api_print_job`, `api_cluster`, `api_printer`, conflict/preheat bridges, pending-job metadata files, direct macro calls, direct raw G-code calls, and duplicated status classification.
- [ ] Decide for each integration whether it remains a client of the native service, moves into `deneb-printsvc`, or becomes a shared Deneb print-control library/API.
- [ ] Avoid preserving patchwork solely for compatibility with a bad stock-driver shape; use compatibility shims only as temporary migration boundaries with removal criteria.
- [ ] Define one Deneb-owned print-control contract for web UI, touchscreen UI, Cura LAN/API, and future services so they do not each reinterpret print state, pending jobs, abort state, preheat state, and macro safety differently.
- [ ] Deduplicate print-control logic as part of the rewrite: status classification, print/pending job metadata, command formatting, macro lookup, safe motion policy, heat-state decisions, pause/resume/abort semantics, and error mapping should each have one owner.
- [ ] Prefer shared native helpers or a single `deneb-printsvc` API over copy-pasted logic between LCD UI, web UI, REST API, Cura cluster API, and diagnostics.
- [x] Add tests around shared print-control helpers before removing duplicate callers so web/touch/API behavior stays aligned during migration.
- [x] Keep the native driver source tree intentionally modular; do not create a few thousand-line catch-all files.
- [x] Start with named source units for clear responsibilities, for example: service main/init, ZMQ IPC, print-control API, serial transport, Marlin packet framing, CRC, status parsing, command parsing, G-code stream reader, macro registry, job queue, heater waits, pause/resume state, abort/finalize motion policy, diagnostics/logging, and test fakes.
- [x] Require new `deneb-printsvc` files to have narrow public headers and focused tests where practical, especially for packet framing, status parsing, G-code streaming, and state-machine behavior.
- [x] Port the Marlin serial packet layer cleanly: `/dev/ttyS1`, 250000 baud, CRC16 packet generation, CRC8 sync parsing, sequence numbers, ACK/reject handling, resend queues, and flow-control limits.
- [x] Port the status parser for M105, M114, G28 home-distance, M115 version data, Marlin log messages, and Marlin fault messages.
- [x] Port bounded G-code streaming for `JOB`, `MACRO`, and raw `GCODE` without loading whole print jobs into RAM.
- [x] Preserve macro-file compatibility with `/home/cygnus/marlindriver/gcode/` while allowing safer Deneb-owned macro overrides later.
- [x] Re-design abort, pause, resume, and print-finish behavior deliberately instead of blindly copying stock motion sequences.
- [x] Fix unsafe abort cleanup as part of the native design: no duplicate homing, no unsafe XY motion into unknown print geometry, and clear status transitions after cancellation.
- [x] Preserve startup motion-controller firmware verification/programming behavior or document and test a safer replacement.
- [x] Add a lab-only init switch so stock `printserver` can be restored without reflashing if native print service fails.
- [x] Add side-by-side logging for stock versus native service status fields, serial ACK/reject rates, resend counts, queue depth, planner starvation indicators, and command latency.
- [x] Add host tests for CRC, packet framing, status parsing, G-code stream replacement, command parsing, and state transitions.
- [ ] Add live-device smoke tests for boot sync, idle status, heat/cool, home, macro execution, USB/local print, Cura-started print, pause, resume, abort during preheat, abort during active printing, print completion, and recovery after service restart.
- [ ] Require before/after RAM, CPU, boot-time, and print-throughput measurements before this replacement can ship outside experimental builds.
- [x] Do not disable stock `printserver` by default until the native service has rollback, diagnostics, and repeated live print validation.

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
- [x] Move uploaded print-file metadata sniffing into
  `common/print/print_job_file.*` so Cura upload registration, conflict
  detection, and future native print-service entry points share one parser for
  `material_guid`, `nozzle_size`, `print_core_id`, and the Deneb upload spool
  path instead of keeping that logic inside the web print-job endpoint.
- [x] Move uploaded print-file spool ownership into
  `common/print/print_job_file.*`: filename sanitizing, Deneb spool-path
  construction, spool directory creation, and rename/copy fallback are shared
  native helpers instead of web-only Cura upload code.
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
- [x] Remove remaining native UI Python launchers from frame lighting, USB
  material-profile import, and diagnostics export: frame lighting sends native
  `M142` G-code directly, material import scans USB/profile XML in C, and log
  export archives with `tar` instead of Python `shutil`.
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
- [x] Move stock print-service command verb names for `GCODE`, `MACRO`, `JOB`,
  `ABORT`, `PAUSE`, and `RESUME` into `common/print/command_format.h` so
  action-frame formatting, LCD backend commands, web backend commands, and
  native `print_control` do not each carry separate raw driver strings.
- [x] Move direct touchscreen/web macro, multi-line G-code, and job-start
  callers onto native backend helper functions so build-plate leveling, jog
  motion, material load/unload, touchscreen print start, touchscreen conflict
  continue, web motion macros, Cura upload start, and Cura pending-job continue
  no longer each hand-roll stock `COMMAND<json>` payloads.
- [x] Move repeated touchscreen/web jog, absolute-position, Z-home, heater
  target, cooldown, and material load/unload G-code string construction into
  `common/print/gcode_command.*` with host tests so remaining raw G-code
  commands used by UI/API safety gates share one Deneb-owned formatter.
- [x] Move web/API manual motion bounds into `common/print/gcode_command.*`
  so jog distance, axis normalization, build-volume limits, and move-speed
  validation are owned beside the G-code formatter instead of inside the REST
  endpoint.
- [x] Move touchscreen frame-lighting `M142` command construction and
  brightness-to-PWM clamping into `common/print/gcode_command.*` so another
  UI-only stock-driver G-code formatter is owned by the shared native layer.
- [x] Move native abort/finish motion-policy heater/fan/stepper cleanup onto
  shared G-code constants and heater-off helpers so print-service safety
  cleanup uses the same cooldown command owner as touchscreen/web controls.
- [x] Move stock macro filenames and remaining pending-job display reads into
  shared native helpers: touchscreen jog/leveling and web motion commands now
  use common macro-name constants, the shared print-state rules own transient
  macro-file filtering, and LCD/API job-name display reads pending-job metadata
  through `pending_job_file` instead of local JSON scans.
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
- [x] Move pending-job presence/path/conflict predicates into
  `common/print/pending_job_file.*` so web upload dedupe, Cura cluster pending
  actions, and touchscreen conflict prompts use one tracker/path/conflict
  contract instead of local `tracker >= 0` and `path[0]` checks.
- [x] Move pending-upload dedupe/block decisions into
  `common/print/pending_job_file.*` so the web print upload endpoint reuses the
  same native pending-job path/display-name comparison when accepting a
  duplicate Cura upload or rejecting a different queued job.
- [x] Move manual-action safety gating into shared native print-state rules so
  web/API motion controls, touchscreen jog controls, temperature actions,
  material moves, and frame-light startup apply all use one connected,
  not-printing, not-paused, not-error predicate.
- [x] Move temperature-target readiness into shared native print-state rules so
  preheat logging, native heater waits, and material move readiness agree on
  bed-only, nozzle-only, combined-target, and tolerance behavior.
- [x] Move print elapsed-time calculation into shared native print-state rules
  so UM API, Deneb API, and Cura cluster API responses clamp `time_total` /
  `time_left` consistently instead of each computing elapsed seconds locally.
- [x] Move print progress percentage calculation into shared native print-state
  rules so LCD and web/API backend status parsing clamp weird `Tleft` values
  the same way during preheat, printing, completion, and abort cleanup.
- [x] Move pending-job metadata persistence into the native pending-job module
  so Cura upload registration and future `deneb-printsvc` entry points share
  the same serialize, temp-write, atomic-rename, and cleanup contract instead
  of keeping the file writer inside the web print-job endpoint.
- [x] Move Cura/touchscreen material catalog XML parsing, GUID validation,
  record storage, and dynamic material-list response assembly into
  `common/print/material_catalog.*` so cluster API uploads and USB material
  imports share native material persistence rules.
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
- [x] Move Cura/pending native job-start identity onto the shared print-state
  defaults so upload auto-start, Cura cluster continue, and touchscreen
  conflict continue no longer carry their own `"deneb-current-job"` literals.
- [x] Move touchscreen local USB job-start identity onto the shared print-state
  source/default UUID helpers so direct file-browser starts no longer carry
  local `"USB"` / `"0"` command metadata literals, and native pending metadata
  uses the same shared source default for its Cura owner field.
- [x] Move print completion history labeling into shared native print-state
  rules so web history and future native diagnostics use the same
  `"completed"`, `"stopped"`, and `"error"` decision.
- [x] Move active/preparing/stoppable print-context decisions into shared
  native print-state rules so the touchscreen Stop button, preheat status, and
  abort cleanup use the same tested contract instead of local screen/backend
  predicates.
- [x] Move print observation construction into shared native print-state rules
  so LCD status rendering, LCD backend status parsing, web backend status
  parsing, and native status parsing do not each hand-build the same
  active/preheat/stoppable context input.
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
- [x] Add the lab-only native print-service init handoff: Deneb preserves
  `deneb.printsvc.enabled=0` by default, preserves an existing lab flag across
  updates, stops stock `printserver` before starting native `deneb-printsvc`,
  and patches the stock `printserver` init script to skip `print_service.py`
  while native printsvc is enabled so the stock path can be restored by setting
  the flag back to `0`.
- [x] Add native pause/resume state-machine tests so paused jobs do not continue
  streaming and preheat-stage pauses resume to preparing instead of pretending
  to be actively printing.
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
- [x] Move native runtime diagnostic counter projection into
  `printsvc/src/runtime_diagnostics.*` so flow in-flight/sent/ACK/resend/reject,
  queued job depth, streamed line number, and planner-starvation fields are not
  refreshed by ad hoc service code, with host tests covering active and idle
  job projections.
- [x] Move native stock-compatible command reply formatting into
  `printsvc/src/command_reply.*` so the `{"status","message"}` response shape
  and JSON escaping are not private service-dispatcher details, with host tests
  for OK, error, escaping, and truncation failure.
- [x] Move native motion-send ownership into
  `printsvc/src/motion_sender.*` so Marlin packet preparation, serial-ready
  behavior, resend packet writes, and multi-command motion-policy dispatch are
  no longer private `service.c` helpers, with host tests for dry-run sends,
  resend lookup, invalid input, and abort-policy expansion.
- [x] Move native macro/file streaming ownership into
  `printsvc/src/macro_runner.*` so macro path resolution, bounded G-code stream
  iteration, abort checks, flow-window waits, motion sends, and motion polling
  are driven by a tested callback contract instead of private service helper
  loops.
- [x] Move native motion-observation ownership into
  `printsvc/src/motion_observer.*` so Marlin status parsing, heater-wait
  updates, ACK/reject/resend accounting, and resend-sequence detection are one
  tested per-line policy instead of inline serial-poll logic.
- [x] Publish native diagnostics for flow in-flight depth, sent/ACK/resend/reject
  counters, queued job depth, streamed job line number, command latency, and a
  planner-starvation indicator so lab comparisons have one service-owned source.
- [x] Add a low-volume native diagnostics log module for
  `/var/log/ultimaker/deneb-printsvc.log` with stock-shaped status fields next
  to native phase, stop-allowed, serial ACK/reject/resend, queue depth, job
  line, command latency, and planner-starvation fields, plus host tests for the
  emitted comparison keys.
- [x] Add lab-gated direct native print-service routing for touchscreen and
  web/API clients: LCD `backend_comm` and web `backend_zmq` keep the stock
  coordinator route by default, but select native `deneb-printsvc` status
  `5555` and command `5556` endpoints when `deneb.printsvc.enabled=1`, with an
  environment override for host/lab debugging.
- [x] Move the stock-vs-native print backend route decision into
  `common/print/print_backend_route.*` with host tests so LCD, web/API, and
  `deneb-printsvc` tests share the same coordinator/native endpoint constants
  and lab-gate parsing instead of duplicating UCI/env logic.
- [x] Publish the selected stock/coordinator versus native print backend route
  through shared native route diagnostics: LCD and web/API backend modules now
  expose route accessors, web status JSON includes the selected backend and
  endpoint URLs, and host tests cover the shared formatter.
- [x] Add a Deneb API route diagnostic endpoint,
  `GET /api/v1/deneb/print_backend`, so lab validation can query the selected
  stock-coordinator/native-printsvc route without parsing the full status
  payload or consulting Python/coordinator state.
- [x] Cross-compile and package `deneb-printsvc` into the `.deneb` release
  artifact without enabling it over stock `printserver` by default.

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
- [ ] Milestone 13: de-python `marlindriver` with native `deneb-printsvc` experimental replacement. See [Section 8](#8-de-python-marlindriver--native-print-service) and [docs/RESOURCE_REDUCTION_PLAN.md](docs/RESOURCE_REDUCTION_PLAN.md#next-measurement-targets).
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
