# Resource Reduction Plan

Deneb assumes the stock firmware is already too constrained by RAM, CPU, boot time, and UI latency. Matching stock behavior is not the final goal; measurable reduction is.

## First Measurements After SSH Bootstrap

- Process list and resident memory.
- Idle CPU and wakeups.
- CPU while printing.
- Free RAM and memory pressure while idle, printing, uploading, updating, and exporting logs.
- Boot-to-ready timing.
- Service startup order.
- Open sockets and listeners.
- Flash and local storage usage.
- Log volume and rotation behavior.

## Release Gates

- New long-running services need measured RAM and CPU budgets.
- Hot paths should prefer event-driven behavior over polling.
- Large files, logs, thumbnails, and uploads should be streamed or parsed in bounded memory.
- Stable releases should block resource regressions unless explicitly scoped as temporary transition builds.

## Current Guardrails

- Stock menu baseline: 33.7 MB VSZ / about 21 MB RSS.
- Current Deneb UI idle snapshot: 2.7-2.8 MB VSZ / about 1.5-2 MB RSS for `deneb-ui --lang en`.
- Current settled idle system sample: about 90% idle after Deneb replaces the stock menu.
- Current `.deneb` package size: about 1.8 MiB after release stripping, with LVGL, static ZMQ, generated i18n fonts, and the embedded C Digital Factory bridge.
- Current update releases also bundle the lightweight web runtime (`lighttpd`,
  `deneb-api`, static web assets, and `deneb-mdns`) plus Cura local-network
  compatibility code. Resource validation for web polling, cluster API polling,
  upload/start, and print actions remains required before Stable release gates
  should rely on the current budgets.
- Deneb installer prunes the dormant stock Python touchscreen UI after a successful native UI smoke test, saving about 884 KiB on disk while preserving `cygnus.menu.menu_settings` for backend imports.
- Deneb installer disables the stock WiFi AP/captive-portal path and hides the
  obsolete `wificonnect`, `nodogsplash` htdocs, and `cygnus-web-assets` trees
  from the live filesystem. These files live in the read-only squashfs base, so
  `.deneb` cleanup removes runtime visibility/reachability but does not reclaim
  base-image flash space.
- Deneb networking is client-only. The installer disables AP-side DHCP/DNS and
  IPv6 router-advertisement services (`dnsmasq` and `odhcpd`) while preserving
  client networking through `netifd` and `udhcpc`, and removes the stale
  `dhcp.wlan` AP DHCP scope. The `dnsmasq` and `odhcpd` binaries remain in the
  read-only base image; the RAM and attack-surface win comes from stopping and
  disabling them.
- Current remaining idle RAM pressure is dominated by stock Python backend
  services: `coordinator.py` at about 22.8 MB RSS and `print_service.py` at
  about 15.1 MB RSS on the latest sample. `print_service.py` is the Marlin
  serial bridge and motion-controller command/status service, so it is a
  clean-room rewrite candidate rather than something to disable.

## Next Measurement Targets

- Test whether the persistent `logread -f -F /var/log/ultimaker/system.log`
  mirror is still needed when diagnostics can capture `logread` output on
  demand; the latest sample shows about 0.8 MB RSS.
- Dedicated milestone: de-python `marlindriver` by building a native
  `deneb-printsvc` replacement for `print_service.py`; the working checklist
  is [UM2C_MODDING_CHECKLIST.md Section 8](../UM2C_MODDING_CHECKLIST.md#8-de-python-marlindriver--native-print-service).
  The live investigation shows it owns `/dev/ttyS1`, verifies/programs the
  motion-controller firmware at startup, handles job/macro/native raw-G-code
  command control, publishes live status on ZMQ, and implements Marlin flow control,
  CRC/framing, resend handling, and pause/resume state. It is a meaningful RAM
  target, but high risk.
- Keep the first native print-service stage compatible with the stock
  print-service contract: status PUB on `127.0.0.1:5555`, command REP on
  `127.0.0.1:5556`, topic `10001`, and raw `COMMAND<json>` framing. This lets
  coordinator, LCD UI, web UI, Cura LAN flow, and Digital Factory assumptions
  remain stable while the Marlin-facing implementation changes underneath.
- The milestone must also unwind Deneb's current patchwork around the Python
  driver. LCD `backend_comm`, web `backend_zmq`, `api_print_job`,
  `api_cluster`, `api_printer`, direct macro calls, raw G-code calls, and any
  remaining status classifiers all need an ownership decision: keep as clients,
  move into `deneb-printsvc`, or replace with a shared Deneb print-control API.
  Conflict/preheat action bridges, pending-job metadata, print-profile defaults,
  uploaded-file metadata parsing, command-level pause/resume control,
  service-context runtime wiring, service-level command handling, and printer
  hostname/GUID identity reads have started moving into shared native helpers instead of embedded
  Python/Gershwin launchers.
- Do not preserve awkward compatibility layers just because they match the
  current Python driver's shape. Any shim kept for migration needs explicit
  removal criteria and tests proving the final Deneb-owned contract is cleaner
  for web UI, touchscreen UI, API, and Cura LAN flows.
- Deduplicate print-control behavior while the native service is introduced.
  Status classification, print/pending job metadata, command formatting, macro
  lookup, safe motion policy, heat-state decisions, pause/resume/abort
  semantics, and error mapping should each have one owner. Shared native helpers
  now cover command formatting and stock command verbs, flat status JSON field
  extraction, pending-job files, print-state rules, backend status
  time/progress normalization, shared stock macro names, and web/API status
  labels. LCD and web backend transports now use the shared command-format
  planner for simple action frames, raw payload frames, and `JOB` path
  extraction before applying local filename retention. Web and touchscreen
  macro, multi-line G-code, and job-start callers now route through native
  backend helper functions instead of each hand-rolling stock command JSON.
  LCD/API immediate print-file starts now share one native start-plan helper for
  path/source/UUID/default-temperature semantics before dispatching backend
  `JOB` commands, so touchscreen USB starts and web/Cura no-conflict starts do
  not preserve separate driver defaults. Pending-job continue dispatch uses the
  same helper while preserving queued path/source/UUID metadata, leaving only
  the transport send in the LCD/web adapters. Cura upload registration now
  dispatches no-conflict immediate starts through
  `printsvc/src/pending_job_registration.*`, leaving only transport-specific
  callbacks in the web layer. Print-start readiness now has one
  shared native rule for connected/not-error/not-paused/not-printing checks
  before any touchscreen, web/Cura, or pending-continue `JOB` dispatch. The
  touchscreen status screen now also asks shared print-state rules for its
  Cooling/Paused/Preparing/Printing/Error/Idle display state instead of
  locally reinterpreting cached backend flags. Build-plate leveling macro-step
  selection now lives in `common/print/buildplate_level.*`, keeping stock macro
  filename ordering out of LVGL screen code while preserving macro-file
  compatibility. Native macro resolution now rejects traversal and non-`.gcode`
  names, prefers Deneb-owned overrides under `/etc/deneb/marlindriver/gcode`,
  and falls back to stock `/home/cygnus/marlindriver/gcode` macros for
  compatibility. Material-profile USB import root/depth/suffix policy and the
  recursive import walker now live in `common/print/material_catalog.*` beside
  native material parser/storage helpers instead of inside the LVGL material
  screen. Stock
  material/nozzle choices, labels, and UCI update command formatting now live in
  `common/print/print_profile.*`, keeping profile-selection policy out of the
  touchscreen material/nozzle screens while preserving the existing stock UCI
  option names. Deneb system language choices, validation, UCI read fallback,
  and save-command formatting now live in `common/print/system_language.*`, so
  touch UI language settings and web/Deneb config endpoints share one native
  policy. The full web system payload also reads language through that helper
  instead of hard-coding English, and the touchscreen About screen now reads its
  stock host GUID/MAC fallback through `common/print/printer_identity.*`
  instead of a local shell helper. Frame-light saved-state defaults, legacy fallback, output
  brightness selection, and UCI save command formatting now live in
  `common/print/frame_light.*`, leaving the touchscreen frame-light screen to
  render controls and dispatch the already-shared `M142` G-code. Diagnostics
  USB mount probing and support-export command construction now live in
  `common/print/diagnostics_export.*`, keeping stock/Deneb log inclusion,
  redaction filters, archive naming, and background export logging out of LVGL
  screen code. Material
  workflow stop/cooldown planning now lives in
  `common/print/material_workflow.*`, keeping default material temperature,
  stock `M401` stop-material dispatch, and nozzle-off cooldown behavior out of
  the touchscreen material screen. The same helper owns material workflow
  status selection for Busy/Moving/Set Target/Cooling/Target Too Low/Ready/
  Heating labels, avoiding another LVGL-only interpretation of heat/move state.
  LCD/API
  job-name display also reads pending-job metadata through the shared helper
  instead of local JSON scans, and the pending display-name fallback has one
  shared owner for name/path-basename/`"none"` handling across native
  pending-job metadata initialization, LCD backend status retention,
  touchscreen labels, and web/API printer responses. Pending-job metadata
  cleanup also goes through the shared helper for LCD/web stop, abort, cancel,
  delete, and print-end paths. Cura cluster pending-action handling and the
  touchscreen conflict prompt also use the shared pending-job presence
  predicate instead of treating tracker values as local control-flow gates.
  Pending-job continue/cancel dispatch sequencing now lives in
  `common/print/pending_job_dispatch.*`, so LCD and web backend adapters share
  the load, plan, readiness, `JOB`/`ABORT` callback selection, and
  finish-action timing while retaining only their transport send callbacks. LCD
  and web/API backends now select native
  `deneb-printsvc` status/command ports directly when `deneb.printsvc.enabled=1`,
  while preserving the stock coordinator route as the default fallback. That
  route decision lives in `common/print/print_backend_route.*` so clients do not
  each duplicate UCI/env parsing or endpoint constants. The same helper now
  formats route diagnostics exposed by web status JSON and LCD/web backend
  accessors, giving lab runs a native way to prove whether a process selected
  stock coordinator or native `deneb-printsvc`. Backend preheat transition
  tracking now lives in `common/print/print_state_rules.*`, so LCD and web/API
  status clients share one tested owner for target-active and target-ready
  transitions instead of carrying parallel local preheat log flags. Active,
  preparing, and stoppable print-context flags are now projected by the shared
  native print-state helper, so LCD and web/API clients no longer bundle those
  filename/request/timing/preheat decisions separately. Web/API heat-target
  endpoints now use `common/print/gcode_command.*` for bounded temperature JSON
  parsing and `M104`/`M140` planning, keeping raw heater G-code limits out of
  the REST layer. Web/API jog and absolute-position requests now plan through
  the same helper, so axis, distance, coordinate, speed, build-volume, and raw
  move-command decisions are native shared print-control logic instead of REST
  endpoint policy. Web/API manual-motion action requests now parse through
  `common/print/manual_motion.*`, so action JSON shape, legacy home fallback,
  unknown-action classification, and macro-vs-G-code selection share the same
  native owner as touchscreen motion helpers. Cura cluster action parsing now
  also uses the shared print-state helper for the pending-job `print` default,
  so a missing action body only turns into continue when pending metadata
  exists. Deneb API route diagnostics now use typed backend accessors instead
  of comparing route display strings, so backend identity classification stays
  with the shared route owner. Later slices should keep collapsing remaining
  duplicate web/UI/API logic toward those helpers or a single
  `deneb-printsvc` API.
- Keep `deneb-printsvc` source files split by responsibility from the first
  scaffold. Expected modules include service/init, ZMQ IPC, print-control API,
  serial transport, Marlin packet framing, CRC, status parsing, command
  parsing, G-code stream reader, macro registry, job queue, heater waits,
  pause/resume state, abort/finalize motion policy, diagnostics/logging, and
  test fakes. Avoid thousand-line catch-all files.
- Treat abort, pause, resume, and print-finish as intentional Deneb behavior,
  not line-for-line ports. The native replacement must remove unsafe abort
  cleanup behavior, avoid duplicate homing, and report clear cancellation
  status before it can replace stock `printserver` outside experimental builds.
- Native diagnostic logging now writes a low-volume comparison stream under
  `/var/log/ultimaker/deneb-printsvc.log` when the lab-gated service is
  running. Each status line places stock-shaped fields such as `stock.req`,
  `stock.file`, temperatures, position, and fault state beside native phase,
  stop-allowed state, serial ACK/reject/resend counters, queue depth, streamed
  line number, command latency, and planner-starvation counters. This gives
  lab runs a stable artifact for comparing native behavior against captured
  stock `printserver` logs without adding Python.
- The native replacement now has an initial buildable C source tree at
  `printsvc/`, is included in release packages as a lab-gated binary, and has
  host tests for the command/status/packet/flow-control/heater-wait,
  G28/home-distance, nonblocking job streaming, motion-firmware verification,
  abort/finish policy, pause/resume state-machine behavior, shared print-control
  contract, shared command formatting and stock command verbs,
  shared flat status JSON field extraction,
  shared JSON-array file fallback reads, shared print-history path ownership,
  shared JSON string escaping,
  shared Cura/UM2C machine-material-nozzle profile defaults and UCI reads,
  shared JSON field presence, strict numeric, and boolean parsing for API
  request bodies,
  shared stock-status truthy value parsing,
  shared stock/native status payload parsing,
  shared uploaded print-file metadata parsing and print spool path ownership,
  pending-job metadata, shared pending-job file
  parsing/display/cleanup for web/touch/API conflict and status flows, shared macro
  names/transient-file filtering, shared web/API status label mapping,
  shared pending-job display-name fallback and native metadata naming,
  shared manual-action safety gating,
  shared temperature-target readiness,
  shared material-move readiness limits,
  shared print elapsed-time calculation,
  shared print progress calculation,
  shared pending-job metadata persistence,
  native command audit policy,
  native job-control policy,
  native active-job streaming policy,
  native macro-command policy,
  native command-dispatch policy,
  native motion runtime policy,
  shared Cura/touchscreen material catalog parsing/storage/response assembly,
  shared touchscreen loaded material/nozzle profile reads,
  shared UM2C nozzle-size normalization,
  shared UM API progress fraction clamping,
  shared job state-or-none naming,
  shared web/Cura print-job action parsing,
  shared current-job fallback identity,
  web REST current-job/status adapter accessors,
  shared Cura/pending native job-start identity,
  shared print completion history labeling,
  shared touchscreen print-file scan roots and candidate filtering,
  shared active/preparing/stoppable print-context decisions,
  LCD status-screen print-context adapter accessors,
  LCD/web manual-action readiness adapter helpers,
  touchscreen/web macro and G-code command helper routing,
  shared backend route diagnostics,
  touchscreen conflict actions and Cura cluster pending-job actions through
  native `JOB`/`ABORT` backend adapter helpers, native Cura upload registration
  planning, no-conflict `JOB` startup, reversible native-vs-stock print service init
  gating, low-volume side-by-side diagnostic logging, native
  frame-light/material-import/diagnostics UI helpers, native
  default pending-upload classification, shared queued upload response formatting,
  shared relative jog command sequencing, shared cooldown command sequencing,
  shared material load/unload command sequencing, shared manual motion action
  planning, shared diagnostics fan command formatting, shared bed/nozzle
  temperature command limits, native
  error mapping, and native diagnostics slices. It is still disabled by default
  and does not yet satisfy the release criteria for replacing stock
  `printserver`.
- The lab-only switch is explicit and reversible in the package scripts:
  `deneb.printsvc.enabled` defaults to `0`, existing lab values are preserved
  across Deneb updates, native `deneb-printsvc` stops stock `printserver` on
  start, and the patched stock `printserver` init skips `print_service.py` only
  while the native flag is `1`.
- Keep `onion-helper` under observation, but do not disable it yet. A live
  stop test showed SSH, Ethernet client networking, `udhcpc`, `deneb-ui`,
  `coordinator.py`, `print_service.py`, and the separate `onion` ubus API
  remained healthy. The daemon exposes generic ubus helper methods for process
  launch, file writes, and downloads, so it is more of an attack-surface
  candidate than a large RAM win: its latest sample shows about 1.2 MB RSS, but
  only about 100 KiB was anonymous/private memory.
- Remove or suppress stock Python `__pycache__` writes only if it does not hurt
  boot/runtime behavior; the latest overlay sample shows about 260 KiB of
  bytecode cache.
- Measure Cura workflows specifically: mDNS advertisement idle cost, repeated
  Cura monitor polling, multipart upload, pending conflict/preheat flow,
  pause/resume/abort actions, and cleanup after failed uploads.

Before treating a build as release-ready, repeat memory and CPU sampling while
idle, printing, installing a Deneb package, exporting diagnostics, switching
languages, and using Digital Factory pairing/disconnect.
