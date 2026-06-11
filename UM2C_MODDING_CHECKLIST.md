# Deneb UM2C Modding Checklist

Date: 2026-06-11

This checklist tracks current engineering status, release blockers, and
publication-risk guardrails. It is not legal advice. Completed items should stay
checked only when there is repo evidence or accepted hardware evidence.

Authoritative status companions:

- Current resource gates: [docs/RESOURCE_REDUCTION_PLAN.md](docs/RESOURCE_REDUCTION_PLAN.md)
- Native print-service evidence: [docs/PRINTSVC_EVIDENCE_LEDGER.md](docs/PRINTSVC_EVIDENCE_LEDGER.md)
- Native print-service integration audit: [docs/PRINTSVC_INTEGRATION_AUDIT.md](docs/PRINTSVC_INTEGRATION_AUDIT.md)
- Stock/native parity notes: [docs/PRINTSVC_LEGACY_PARITY_AUDIT.md](docs/PRINTSVC_LEGACY_PARITY_AUDIT.md)
- Web and Cura status: [docs/WEB_UI.md](docs/WEB_UI.md), [docs/CURA_INTEGRATION.md](docs/CURA_INTEGRATION.md)
- Touchscreen parity status: [docs/STOCK_UI_COVERAGE.md](docs/STOCK_UI_COVERAGE.md)

## Current Open Focus

1. Prove LCD and Web UI hands-on workflows against the native print service:
   queued print, start, pause/resume, abort, completion, stale-state recovery.
2. Prove real desktop Cura discovery/upload/start/actions with current Cura
   builds, not only generated cluster-API fixtures.
3. Resolve or explain the native print-service active-soak RSS/private-memory
   staircase with longer heat/motion/job loops.
4. Finish `.deneb` manifest, rollback, signing, release-channel, and restore
   UX before calling any package stable.
5. Keep reducing stock Python backend dependency without copying vendor code or
   overclaiming parity.

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
- [ ] Validate real desktop Cura discovery, upload/start, monitor polling,
  pause/resume/abort/delete, pending-job visibility, and failed-upload cleanup
  against current Cura builds.
- [ ] Hardware-test Web UI user workflows separately from API smoke evidence.
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
- [ ] Prove LCD hands-on workflow against native service.
- [ ] Prove Web UI hands-on workflow against native service.
- [ ] Prove desktop Cura client workflow, not just generated cluster API routes.
- [ ] Prove Digital Factory lifecycle behavior beyond observe-only bridge status.
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
- [x] Diagnostics export and Deneb-specific print-service diagnostics exist.
- [ ] Add remote-control authorization/audit story for web/API/Cura actions.
- [ ] Provide touchscreen-visible indication when remote control is active.
- [ ] Keep `onion-helper`, stock Python services, pycache growth, and backend
  service dependency under review before disabling or replacing more services.
- [ ] Measure print-quality regressions and correlate them with CPU, memory,
  streaming cadence, serial flow control, and service load.
- [ ] Modernize OS/service pieces only when measurement shows a practical win
  and the rollback path is understood.

## 10. Motion Controller / Marlin Firmware

- [ ] Defer Marlin firmware changes until Deneb's Linux-side UI, web/API, Cura,
  installer, rollback, and native print-service behavior are proven.
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
| Cura compatibility | Initial implementation exists; desktop Cura proof remains |
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
