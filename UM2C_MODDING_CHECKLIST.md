# Deneb UM2C Modding Checklist

Reconciled: 2026-07-10

> **Machine-audited acceptance inventory:** release scripts validate portions
> of this file at its current path. It is not the current project dashboard or
> priority queue. Use [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) for done,
> in-progress, planned, blocked, and broken work, and use
> [docs/PLATFORM_MODERNIZATION_ROADMAP.md](docs/PLATFORM_MODERNIZATION_ROADMAP.md)
> for future sequencing.

A checked item means only that its exact scoped statement is supported by source,
host, package, or dated target evidence. It does not make its section, feature,
or release complete. Preserve explicit `SOURCE`, `HOST`, `TARGET`, `FAILED`, and
`BLOCKED` boundaries when editing acceptance items.

Related machine-audited specifications:

- [Native print-service evidence](docs/PRINTSVC_EVIDENCE_LEDGER.md)
- [Native print-service integration ownership](docs/PRINTSVC_INTEGRATION_AUDIT.md)

Documentation navigation and historical evidence are indexed in
[docs/README.md](docs/README.md).
## 1. Project Boundary And Legal Guardrails

- [x] Project name is `Deneb`, documented as an independent community mod and
  not an UltiMaker-endorsed firmware.
- [x] Repository boundary is original addon/mod code, build scripts, installer
  scripts, docs, compatibility layers, and minimal transformation logic.
- [x] Repo includes MPL-2.0 license, compliance notes, third-party notices, and
  safety warnings.
- [x] `.gitignore` excludes extracted firmware trees, generated firmware
  images, device secrets, logs, and private keys.
- [ ] Audit copied/adapted snippets before publication. Do not commit full files
  from `/home/cygnus`, extracted firmware, decompiled vendor code, or unclear
  vendor-owned trees.
- [ ] Add SPDX/REUSE-style coverage if component licenses become mixed beyond
  the current MPL-focused addon boundary.
- [ ] Compare Deneb notices against the stock touchscreen/legal disclosure and
  package inventory before public release.

## 2. Confirmed Platform Facts

- [x] Target is OpenWrt/MIPS on an Onion Omega2+-class platform.
- [x] Stock application layer is Python-heavy under `/home/cygnus`.
- [x] Stock touchscreen is launched by `/etc/init.d/menu` and uses the Linux
  framebuffer/input stack.
- [x] Stock print service exposes ZMQ status/command behavior used by
  coordinator, UI, web/API, Cura compatibility, and Digital Factory paths.
- [x] USB update flow extracts update payloads and runs `update.sh`; generic
  Onion `autorun.sh` is intentionally disabled in the inspected image.
- [x] SSH bootstrap evidence established `root` as the interactive login path on
  the inspected device/image.

## 3. Delivery, Installation, And Updates

- [x] SSH bootstrap package exists and enables the development lane.
- [x] `.deneb` update packages are buildable and install Deneb-owned UI, web,
  API, mDNS, native print-service, smoke tools, audits, init scripts, notices,
  and manifest data.
- [x] Installer backs up touched files and preserves a Deneb version marker.
- [x] Installer/package gates reject native print-service packages that omit the
  required smoke, audit, release-gate, init, and manifest evidence tooling.
- [x] Experimental packages are the default native print-service channel.
- [ ] Add complete installed-file manifest details: hashes, rollback metadata,
  and operator-readable rollback instructions.
- [ ] Implement and verify rollback mode.
- [ ] Add Deneb package signing: public verification key in repo, private key
  outside repo, checksums/signatures in artifacts, and target-side verification
  before install.
- [ ] Build release-channel UX for Stable/Nightly, with no silent updates and no
  default telemetry.
- [ ] Preserve and document a deliberate official-firmware restore path.
- [ ] Test install, reboot persistence, failed-install recovery, rollback, and
  restore on spare hardware before recommending use.

## 4. Resource Reduction And Release Gates

- [x] Treat RAM, CPU, boot time, disk footprint, log growth, sockets, file
  descriptors, and UI latency as release gates.
- [x] Native touchscreen UI showed a large idle-process reduction versus the
  stock Python menu in accepted measurements.
- [x] Native print-service bounded stock/native comparison currently shows lower
  driver RSS and system memory for the accepted physical fixture set.
- [x] Native print-service release tooling blocks non-experimental package
  channels unless stock/native summaries pass verifier and strict comparator
  checks.
- [x] Diagnostics log growth from native print-service flow-counter churn was
  mitigated with throttling and startup truncation.
- [ ] Establish broader stock baselines for booting, printing, paused,
  uploading, updating, diagnostics export, web polling, and Cura monitor paths.
- [ ] Run Valgrind Memcheck where practical for host-buildable native C changes,
  especially print-service, shared print-control, parser, and streaming changes,
  so leak/resource regressions are caught before hardware-only validation.
- [ ] Prove long active heat/motion/job stability, including resident memory,
  private memory, tmpfs/log growth, fd count, thread count, and process restarts.
- [ ] Require every new long-running service to carry measured RAM/CPU budgets
  and a release reason.
- [ ] Keep features lazy, bounded, streaming-friendly, and event-driven wherever
  possible.

## 5. Touchscreen UI

- [x] Replace stock Python touchscreen frontend with original LVGL v9 C UI.
- [x] Native UI starts as `deneb-ui` instead of rewriting large portions of
  stock `/home/cygnus/menu`.
- [x] UI source is original and publishable.
- [x] Current UI covers Home, Status, Print from USB, Temperature, Material,
  Manual Control, Network, Settings, Maintenance, Diagnostics, Digital Factory,
  About, and related setup/status screens.
- [x] Current UI includes client WiFi/Ethernet USB setup, material/nozzle
  settings, diagnostics export, frame lighting, language selection, update
  entry points, and boot/status presentation.
- [x] Touchscreen print-state fixes cover boot idle Stop state, preheat Stop
  availability, mismatch continue flow, and abort cleanup/status handling.
- [ ] Hardware-proof the full screen catalog across idle, printing, paused,
  error, material, update, network, recovery, and populated-USB states.
- [ ] Add watchdog/logging for slow page transitions, render stalls, backend
  calls, and UI memory growth.
- [ ] Complete richer stock-parity gaps called out in
  [docs/STOCK_UI_COVERAGE.md](docs/STOCK_UI_COVERAGE.md), including preview and
  recovery/error states where still missing.

## 6. Web UI, API, And Cura

- [x] Lightweight web runtime is packaged: lighttpd, static assets, `deneb-api`,
  and `deneb-mdns`.
- [x] Web surface covers status, current job, temperatures, pause/resume/cancel,
  heat/cooldown, and guarded X/Y/Z motion.
- [x] API includes an initial UltiMaker REST v1-shaped compatibility surface and
  single-printer `/cluster-api/v1/` routes.
- [x] Cura mDNS advertisement and Deneb Cura plugin exist for mapping
  `deneb_um2c` to Cura's stock UM2+ Connect profile.
- [x] Cura/cluster upload-start path, pending-job metadata, conflict
  continue/cancel, and cluster action routing are implemented through shared
  native helpers where current audits require it.
- [ ] Validate remaining real desktop Cura gaps against current Cura builds:
  failed-upload cleanup and delete behavior if current Cura still exposes that
  path. Discovery, upload/start, material-mismatch Continue/Cancel, monitor
  polling, pause/resume, cancel/abort, pending-job visibility, pending recovery
  after restarts, and package `9cdb5d6f` S5-style progress/time reporting are
  already proven for the 2026-06-14 Cura 5.13 runs.
- [ ] Hardware-test Web UI user workflows separately from API smoke evidence.
  Live Web UI status/progress/time-left display was proven during the
  2026-06-14 package `9cdb5d6f` print after the timer label was made explicit;
  Web UI pause/resume/cancel and stale-state recovery remain open.
- [ ] Measure web/API resource behavior under polling, upload, motion, heat, and
  print-action load.
- [ ] Keep extruder jog out of web controls until safe-temperature gating exists.

## 7. Native Print Service

Current status: experimental native replacement track exists and is packaged,
but it remains blocked from non-experimental promotion by open client-flow and
long-soak proof.

- [x] Build original native `deneb-printsvc` replacement for the stock Python
  `marlindriver` print service.
- [x] Preserve the stock ZMQ compatibility boundary first: status PUB
  `127.0.0.1:5555`, command REP `127.0.0.1:5556`, topic `10001`, and
  `COMMAND<json>` request framing.
- [x] Route Deneb LCD/web/API clients to the native print-service endpoints
  through shared backend-route helpers.
- [x] Package native service, init handoff, macros, smoke harnesses, stock
  baseline collector, verifier, comparator, stability runner, active-soak
  runner, and static/package audits.
- [x] Gate archives and installers against stock Python driver artifacts in
  native packages.
- [x] Modularize native source across IPC, command parsing, serial transport,
  packet/CRC, flow control, status parsing, job streaming, macro registry,
  heater waits, pause/resume, lifecycle, diagnostics, and tests.
- [x] Host coverage exists for key parser, packet, flow-control, command,
  state-machine, helper, audit, and release-gate paths.
- [x] Valgrind/ASan host memory tooling exists for the native print-service test
  surface and should be part of validation where host execution is possible.
- [x] Accepted bounded hardware evidence covers native route ownership,
  observe-only firmware/temperature telemetry, low-temperature heat/preheat
  abort, active abort cleanup, bounded pause/resume, generated cluster API
  upload/start/abort, completion flow drain, native driver RSS reduction,
  diagnostics-log mitigation, short repeated-job stability, and strict
  stock/native resource comparison for the current bounded evidence set.
- [ ] Prove LCD hands-on workflow against native service. Some start/abort and
  completion slices pass, but the latest physical Pause test failed and
  material/leveling flows remain incomplete.
- [ ] Prove Web UI hands-on workflow against native service. Live status,
  progress, and time-left display are proven on package `9cdb5d6f`; controls
  and stale-state recovery remain open.
- [ ] Close remaining desktop Cura client gaps after the proven Cura 5.13
  local-network workflow: broader failure modes.
- [x] Prove the scoped Digital Factory lifecycle and representative remote-print
  behavior with native `deneb-dfsvc` for pairing, reconnect, disconnect,
  rename, conflict Continue/Cancel, completion, and abort without stock
  `connector.py`. Broader client and soak proof remains open; this is not a
  full release-readiness claim.
- [ ] Prove broader real slicer output for completion, pause/resume, and abort.
- [ ] Complete multi-hour active heat/motion/job soak with acceptable memory and
  log behavior.
- [ ] Remove or narrow remaining compatibility shims once hardware client proof
  shows they are display/transport adapters only.

## 8. Local Storage And Generic Slicer Work

- [x] USB file browser and native/local print entry paths exist.
- [x] Upload destination, filename sanitizing, pending-job metadata, material
  upload parsing, and job-summary formatting have shared native helpers.
- [ ] Validate USB/local print workflows on populated media with real files.
- [ ] Prove generic slicer output across realistic geometry, duration, heating,
  cooling, pause/resume, abort, and completion paths.
- [ ] Add clear unsupported-G-code handling where native/stock behavior is not
  yet known.
- [ ] Keep large print files streamed and avoid full-file reads in UI/API paths.

## 9. Reliability, Security, And Modernization

- [x] Client-only WiFi/Ethernet setup exists via USB import.
- [x] Stock AP/captive-portal setup and AP-side DHCP/DNS/IPv6 services are
  disabled/hidden for Deneb installs where applicable.
- [x] SOURCE: diagnostics export formatting and Deneb-specific print-service diagnostics exist.
- [ ] TARGET: package, install, and prove bounded redacted diagnostics export/download.
- [ ] Add remote-control authorization/audit story for web/API/Cura actions.
- [ ] Provide touchscreen-visible indication when remote control is active.
- [x] Disable stock `compile_all` for Deneb installs and clear
  `/home/cygnus/__pycache__` after backing up the stock init script.
- [x] Replace the active Digital Factory connector/init path with native `deneb-dfsvc`. Scoped pairing, reconnect, disconnect, representative cloud print/actions, and rename are target-proven; broader client and soak coverage remains open.
- [ ] Keep `onion-helper`, remaining stock Python services, and backend service
  dependencies under review before disabling or replacing more services.
- [ ] Measure print-quality regressions and correlate them with CPU, memory,
  streaming cadence, serial flow control, and service load.
- [ ] Modernize OS/service pieces only when measurement shows a practical win
  and the rollback path is understood.

## 10. Motion Controller / Marlin Firmware

- [ ] Defer Marlin firmware changes until Deneb's Linux-side UI, web/API, Cura,
  installer, rollback, and native print-service behavior are proven.
- [x] Audit public Connect source for added/removed G/M codes, modified
  responses, framing, and modern-Marlin command conflicts. This is source
  evidence only; see `docs/evidence/MARLIN_COMMAND_PROTOCOL_AUDIT.md`.
- [ ] Freeze versioned command, parameter, response, stop-reason, and transport
  fixtures from source plus safe controller traces.
- [ ] Implement a bounded host preflight/controller adapter for deterministic
  rewrites, protocol-version selection, planner backpressure, and response
  normalization; do not build a general arbitrary G-code interpreter.
- [ ] Keep thermal, endstop, power, stepper, flow, watchdog, emergency-stop, and
  controller-attached fan/light authority inside the MCU.
- [ ] Compare standard numbered/checksummed modern-Marlin transport with the
  Connect CRC16 protocol using corruption, lost/duplicate line, resend,
  controller-reset, queue-full, and sequence-resynchronization tests.
- [ ] Prototype modern `ADVANCED_OK` plus `EMERGENCY_PARSER`; measure planner
  and command-buffer reporting, M410 interruption latency, AVR flash/RAM cost,
  and recovery before retaining any custom transport code.
- [ ] Resolve `M290`, `M401`, and `M405`-`M407` collisions without
  silently changing behavior expected by Deneb.
- [ ] Publish tested Cura, PrusaSlicer, OrcaSlicer, and other supported profiles
  with reviewed start/end G-code and representative generated-job fixtures.
- [ ] Add preflight classification for pass-through, rewritten, Deneb-only,
  controller-private, unsupported, and unsafe/conflicting commands.
- [ ] Port and prove Connect board detection, ADC/thermal, power, TMC2130,
  flow-sensor, fan/light, watchdog, endstop, and fault behavior.
- [ ] If Marlin work starts, keep it legally separate and GPL-compatible.
- [ ] Treat motion-controller flashing/recovery as a high-risk workflow with
  dedicated hardware proof and restore documentation.

## 11. Milestone Snapshot

| Milestone | Status |
| --- | --- |
| Private repo boundary and legal guardrails | Mostly complete; publication audit remains |
| SSH bootstrap | Complete for development use |
| Native touchscreen UI | Implemented; broader real-state hardware proof remains |
| Web/API runtime | Implemented; hands-on and resource validation remain |
| Cura compatibility | Cura 5.13 local-network happy path and S5-style progress/time proven; failure modes remain |
| `.deneb` installer | Functional experimental lane; rollback/signing/stable release hardening remain |
| Native print-service replacement | Experimental with strong bounded evidence; client workflows and long soak remain |
| Stable public release | Not ready |

## 12. Do Not Do

- Do not publish full firmware images, extracted root filesystems, proprietary
  application source, decompiled vendor code, private keys, or device secrets.
- Do not mark compatibility complete from static code review alone.
- Do not call native print-service stable while LCD/Web/Cura/Digital Factory
  workflows and active-soak behavior remain unproven.
- Do not expose unauthenticated heat, motion, print, or raw G-code controls on
  untrusted networks.
- Do not replace official firmware restore/trust behavior with Deneb signing
  without a deliberate user-visible restore model.
