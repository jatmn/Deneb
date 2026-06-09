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
- The installer starts `deneb-api`, `deneb-web`, and `deneb-mdns` immediately
  after updating their binaries in addition to enabling them for the next boot,
  so a delayed or timed-out reboot does not leave lighttpd waiting on a missing
  `/var/run/deneb-api.sock`.
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
  Legacy Python source anchors and no-overclaim rules are tracked in
  [docs/PRINTSVC_LEGACY_PARITY_AUDIT.md](PRINTSVC_LEGACY_PARITY_AUDIT.md).
  The live investigation shows it owns `/dev/ttyS1`, verifies the
  motion-controller firmware cache at startup, can invoke programming only when
  explicitly enabled, handles job/macro/native raw-G-code command control,
  publishes live status on ZMQ, and implements Marlin flow control, CRC/framing,
  resend handling, and pause/resume state. It is a meaningful RAM target, but
  high risk.
- Native status parsing now recognizes stock firmware/version data when it is
  observed: old-Marlin `MACHINE_TYPE`, `PCB_ID`, and quoted `BUILD` lines plus
  generic `FIRMWARE_NAME` output can be carried into native status JSON as
  `firmware`, `machineType`, `pcbId`, and `pcbIdValid`, and the shared
  status-payload parser retains those fields for clients. Stock Python source
  review shows the normal status loop schedules `M105` and `M114`, while
  version output is parsed only when observed and `firmware` defaults to
  `none`. Native startup now sends `M115` with the startup `M105`/`M114` probe
  and retries `M115` a bounded number of times while firmware metadata remains
  absent, while the recurring poll stays on the stock-shaped `M105`/`M114`
  cadence. Live parity must compare stock and native on the same firmware:
  `firmware:"none"` is acceptable if stock also reports `none`; non-`none`
  metadata is required only when the motion controller actually emits version
  output. The smoke harness now exposes this as an observe-only
  `--firmware-proof` summary phase and the stock/native comparer rejects
  native firmware fallback or missing ambient bed/nozzle telemetry when stock
  captured real values.
- Native M105 telemetry parsing also carries the stock topcap fields: compact
  old-Marlin `t1/33.2` reports now update `topcapIsPresent` and
  `topcapTemperature` in the native status JSON instead of leaving clients on
  default values.
- Web/API status plumbing now carries those native fields beyond the parser when
  the native service supplies them:
  `backend_zmq` keeps firmware/version and topcap telemetry in backend state,
  cached Deneb status JSON exposes them, and the shared printer response
  formatter includes them in `/api/v1/printer` and `/api/v1/airmanager`
  responses.
- Keep the first native print-service stage compatible with the stock
  print-service contract: status PUB on `127.0.0.1:5555`, command REP on
  `127.0.0.1:5556`, topic `10001`, and raw `COMMAND<json>` framing. This is
  the compatibility boundary intended to let coordinator, LCD UI, web UI, Cura
  LAN flow, and Digital Factory assumptions remain stable while the
  Marlin-facing implementation changes underneath; hardware proof for those
  clients remains an open Section 8 gate.
- The milestone must also unwind Deneb's current patchwork around the Python
  driver. LCD `backend_comm`, web `backend_zmq`, `api_print_job`,
  `api_cluster`, `api_printer`, direct macro calls, raw G-code calls, and any
  remaining status classifiers all need an ownership decision: keep as clients,
  move into `deneb-printsvc`, or replace with a shared Deneb print-control API.
  Conflict/preheat action bridges, pending-job metadata, print-profile defaults,
  uploaded-file metadata parsing, command-level pause/resume control,
  service-context runtime wiring, service-level command handling, and printer
  hostname/GUID identity reads have started moving into shared native helpers.
  LCD/web ZMQ status consumers now also share
  `common/print/status_state.*` for status-to-state application, retained
  filename cleanup, stop-guard cleanup, timing normalization, and
  active/preparing/stoppable context derivation. Their lifecycle transition
  logging and preheat event logging now also use shared `status_state.*`
  wrappers instead of carrying separate pause/resume/completion/preheat
  classifiers beside each backend or embedded Python/Gershwin launchers. The
  web backend's cached JSON now reuses its request/native-aware status-label
  accessor, avoiding a second label path that could report idle while native
  preheat or abort state is active. Host
  regression coverage now exercises that shared owner for active/preheat stop
  allowance, retained filename behavior through transient macro statuses,
  firmware/topcap identity copying, and immediate stop-guard clearing once the
  current native status is idle.
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
  not preserve separate driver defaults. UM API and Deneb API current-job
  response bodies now use shared native print-job summary formatters instead of
  endpoint-local active-job JSON assembly, and UM API current-job scalar
  endpoints use the same formatter owner for escaping and numeric fields.
  UM printer root/status responses also moved to a shared native status
  formatter so pending filename fallback and top-level print flags are not
  preserved inside the REST endpoint. Bed, head, extruder, hotend, position,
  and feeder subresource response bodies now use the same native formatter
  owner instead of repeating stock printer JSON fragments in `api_printer`.
  Material, LED, ambient, and Air Manager compatibility responses also moved
  behind that formatter, including topcap-derived Air Manager status and LED
  scalar defaults. The web backend adapter now exposes a native printer-status
  response snapshot so REST endpoints do not copy cached backend state fields
  before formatting printer responses. Motion-plan and manual-motion error
  responses now live beside the native G-code/manual-motion planners instead
  of being duplicated in `api_printer`. Cura cluster pending-job continue/cancel
  planning now lives in shared print-state rules, so conflict-resolution
  dispatch commands, failure messages, and pending-dispatch cleanup boundaries
  are no longer encoded in `api_cluster`. The same print-state owner now supplies
  print-action parse/unknown response bodies for Cura cluster and UM print-job
  endpoints. Normal print-job pause/resume/abort/stop dispatch selection now
  lives in `common/print/print_action_dispatch.*`, leaving REST endpoints to
  provide backend transport callbacks instead of switching over action-plan
  kinds locally. Cura cluster print-job delete planning also moved into shared
  action rules/dispatch, so active-job abort versus idle pending-file cleanup
  is not decided inside `api_cluster`.
  Pending-job continue dispatch uses the
  same helper while preserving queued path/source/UUID metadata, leaving only
  the transport send in the LCD/web adapters. Touchscreen conflict prompt
  metadata extraction now also lives in `common/print/pending_job_file.*`, so
  LVGL only renders localized copy for shared job/material defaults, pending
  state, and conflict flags. Default pending/conflict existence checks moved
  into the same helper so Cura cluster action routing and the touchscreen
  conflict trigger no longer load/classify pending metadata locally. Cura upload registration now
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
names, and packages Deneb-owned macro defaults under
`/etc/deneb/marlindriver/gcode` so normal native macro execution no longer
depends on stock `/home/cygnus/marlindriver/gcode` files; resolver tests now
cover Deneb macro resolution with no stock directory and fail-closed behavior
when a macro is missing. The compiled macro directory constants now name
`/etc/deneb/marlindriver/gcode` as the only native macro source.
Material-profile USB import root/depth/suffix policy and the
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
  Native `deneb-printsvc` startup now fails closed when the Marlin serial
  device cannot be opened. Dry-run packet generation remains available only
  behind the explicit `--dry-run` CLI flag for host/lab debugging, so packaged
  device startup cannot silently accept print commands without a motion link.
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
  finish-action timing while retaining only their transport send callbacks.
  Cura upload storage planning now lives in `common/print/print_job_file.*`,
  so filename sanitizing and destination spool-path selection happen before
  pending dedupe, file storage, native registration, and accepted-response
  formatting without preserving that path policy in the web endpoint.
  Multipart upload extraction now lives in the named native web API module
  `web/src/api_multipart.c`, keeping Cura print/material upload parsing shared
  without growing the server main loop into another catch-all file. Cura
  material-upload store-and-cleanup handling now lives in
  `common/print/material_catalog.*`, keeping temp-file cleanup and default
  catalog persistence with the native material catalog owner. LCD and
  web/API backends now select native
  `deneb-printsvc` status/command ports directly. That endpoint mapping lives
  in `common/print/print_backend_route.*` so clients do not each duplicate
  native print-service constants. The same helper now formats route diagnostics
  exposed by web status JSON and LCD/web backend accessors, giving lab runs a
  native way to prove that a process selected `deneb-printsvc`. Backend preheat transition
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
  exists. Cura cluster active-job response formatting now lives in
  `common/print/print_job_summary.*`, so cluster job metadata, configuration,
  build-plate, printer assignment, and compatible-family JSON shape share the
  native print-summary owner instead of endpoint-local assembly. Deneb API
  route diagnostics now use typed backend accessors instead
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
- The service context now passes service-owned serial readiness into both the
  motion runtime and job streamer by reference, so print streaming, finish
  policy dispatch, and dry-run/test paths share one transport readiness state
  instead of copying it into a single adapter. Active job stream-send failures
  now close the stream, clear the active-job flag, and record command or serial
  lifecycle errors so clients do not keep seeing a stale printing state after
  native output fails. Job-streamer abort requests now also route through the
  shared lifecycle abort owner, clearing file identity and timing along with the
  active-job flag and consumed abort latch. Service-level abort now enters a
  native `aborting` phase while serial cleanup commands remain in the flow
  window, then clears file identity and stop state only after those commands
  drain. This keeps the useful stock Python callback timing without copying
  stock's unsafe XY/Z homing cleanup. Idle job-stream polling and service
  close cleanup also clear stale abort latches so stop state cannot survive
  after the active stream is gone. Terminal job paths also clear native
  heater-wait/preheat state so later status observation cannot revive
  `preparing` after abort, completion, or stream failure. Native job EOF now
  starts a finish-cleanup pending phase instead of completing immediately:
  status remains `printing`, diagnostics still report an active native job, and
  lifecycle completion is delayed until the finish policy's flow-window work
  drains. Once complete, native status clears active file/source/UUID/heater
  targets to match the stock service callback's active identity cleanup.
- Native G-code rewrite ownership now covers a stock-derived host-tested slice:
  `gcode_rewrite.*` skips display-only `M117`, converts `M109` to `M104` and
  `M190` to `M140`, expands `G280` prime commands for streamed jobs/macros,
  and marks line-level wait commands so job and macro execution pause in
  service code until the relevant heater reaches target. Raw `GCODE` batches
  now use a bounded native queue so commands after rewritten `M109`/`M190` are
  not sent until the relevant service-side heater wait has cleared. This
  removes a class of old-Marlin busy-command blocking from native streamed and
  raw command paths, but hardware heat/motion proof remains open.
- Treat abort, pause, resume, and print-finish as intentional Deneb behavior,
  not line-for-line ports. The native replacement must remove unsafe abort
  cleanup behavior, avoid duplicate homing, and report clear cancellation
  status before it can replace stock `printserver` outside experimental builds.
  Native pause/resume now has a host-tested physical policy based on the stock
  Python save-position, retract/park/cool, reheat, restore-position, E, and R0
  flow. Active pause now saves the restore coordinates only after a drained
  `M114` probe, matching the stock callback ordering instead of trusting the
  last periodic position cache. Deneb-owned build-volume clamping and a
  streaming gate hold queued print lines until the restore policy drains.
  Abort and finish cleanup
  policy dispatch now reports serial faults when transport is marked ready but
  cleanup G-code cannot be sent, preventing false
  successful abort/completion status on cleanup-send failure. Internal latched
  abort requests now route through the same service-level abort owner as
  explicit `ABORT` commands, so they cannot bypass cleanup and report idle
  early. Marlin resend
  handoff is now covered by the same fail-visible rule: if the controller asks
  for a resend and the native runtime cannot write that resend while the serial
  transport is marked ready, the service records a serial error instead of
  hiding the transport failure. Repeated protocol rejects for the same
  in-flight packet now trip the same serial-error path after a bounded retry
  count instead of replaying the stale packet forever. Old-Marlin ProtoError
  sequence mismatches now resync only while idle or during cleanup; active print
  desync clears in-flight state and forces the job toward abort cleanup instead
  of silently continuing to stream moves. Motion-send return codes now distinguish
  invalid commands, flow-window pressure, and serial transport failure, and
  policy dispatch preserves those named codes instead of flattening cleanup
  failures into generic errors. A shared native mapping helper now classifies
  raw `GCODE`, `MACRO`, active job-stream sends, abort cleanup, and finish
  cleanup as command or serial faults without preserving the Python driver's
  generic command-error behavior. Service-level idle abort commands now fail
  explicitly without sending cleanup motion or keeping `abort_requested`
  latched, while stale active status can still be cleaned back to idle. Macro
  execution preserves the same callback
  failure reason through the bounded macro runner for both macro line sends and
  motion polling, letting native `MACRO` command handling report serial
  transport faults instead of flattening them into generic macro failures.
- Native diagnostic logging now writes a low-volume comparison stream under
  `/var/log/ultimaker/deneb-printsvc.log` when the native service is running.
  Each status line places stock-shaped fields such as `stock.req`,
  `stock.file`, temperatures, position, and fault state beside native phase,
  stop-allowed state, serial ACK/reject/resend counters, queue depth, streamed
  line number, command latency, and planner-starvation counters. This gives
  lab runs a stable artifact for comparing native behavior against captured
  stock `printserver` logs without adding Python.
- The native replacement now has an initial buildable C source tree at
  `printsvc/`, is included in release packages as the default print backend, and has
  host tests for the command/status/packet/flow-control/heater-wait,
  G28/home-distance, old-Marlin indexed `T0:` temperature telemetry,
  nonblocking job streaming, motion-firmware verification,
  abort/finish policy, active-print pause/resume motion policy, shared print-control
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
  audited unknown-command routing,
  native IPC command-frame handling,
  built-in smoke routing through the native IPC frame helper,
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
  planning, no-conflict `JOB` startup, native-only print service init
  handoff, low-volume side-by-side diagnostic logging, native
  frame-light/material-import/diagnostics UI helpers, native
  default pending-upload classification, shared queued upload response formatting,
  shared relative jog command sequencing, shared cooldown command sequencing,
  shared material load/unload command sequencing, shared manual motion action
  planning, shared diagnostics fan command formatting, shared bed/nozzle
  temperature command limits, native
  error mapping, and native diagnostics slices. It still needs live validation
  before it can leave the experimental lane. Earlier 2026-06-08 active-abort
  evidence is no longer treated as sufficient because later full-matrix work
  exposed active/abort ProtoError desync and stale native status risk. The
  native source now has host coverage for that desync path, but accepted
  Section 8 evidence still requires a supervised rerun that proves active
  `printing` with Stop allowed, native `aborting` while cleanup drains, final
  idle with native active/stop flags false, and no manual cluster cleanup. The
  same full-matrix attempt also exposed that UM2C Z homes to max travel, so
  generated completion fixtures now use bounded relative `G1 Z-0.20 F30` moves
  away from homed Z max instead of moving farther into the max endstop.
  Local native flow-control tests now cover late acknowledged
  resends, compact CRC ACK/reject packets, and stale unreplayable resend
  resync; the full heat/motion/Cura/completion and stock/native
  resource-comparison matrix is still required for non-experimental builds.
  After an interrupted full-matrix attempt, the smoke harness now fails closed
  for heat, homing, macro motion, print starts, abort-path jobs, Cura jobs, and
  completion jobs unless `--physical-ok` or
  `DENEB_PRINTSVC_SMOKE_PHYSICAL_OK=1` is set, so physical validation must be
  intentionally supervised and preferably split into narrow checks. More than
  one physical phase in a single run now also requires
  `--physical-bundle-ok` or `DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK=1`.
  Macro/job-style phases that can move after setup also require a guarded
  `z_home` pre-home step before continuing and record
  `reason=pre_physical_home`
  evidence, with all-axis `home` reserved for explicit supervised overrides.
  Each physical phase now also writes a mandatory
  `phase=*-safety kind=physical` plan with axes, required homing, expected
  travel/range, and stop conditions before it can be counted by the verifier.
  The smoke verifier and native audit selftests reject missing safety-plan
  records. This is still only a harness contract; the live heat, motion, Cura,
  pause/resume, abort, completion, and stock/native resource matrix remains
  required before this can leave experimental builds.
- A June 9, 2026 bounded Z-only completion rerun on
  `dist/Deneb_Update_7f070d7.deneb` passed the native smoke verifier after two
  native fixes: late post-`Complete` flow-control desyncs no longer force a
  serial fault once there is no active print phase, and API/touch UI completion
  labeling uses the final native request so short completions log as completed
  instead of stopped. The live run proved native-only route, no stock
  `print_service.py`, initial/final idle with native active/Stop false, Z-home
  safety evidence, and matching `deneb-api` plus `deneb-ui` completion logs.
  This narrows the completion regression but does not close Cura,
  pause/resume, representative print, or stock/native resource evidence.
- The native print-service handoff is owned by Deneb package scripts: native
  `deneb-printsvc` stops stock `printserver` on start, and the patched stock
  `printserver` init shim no longer launches the old driver from
  Deneb-authored code or delegates back through a Deneb config flag. Both the
  native init and the shim clear stale `/var/run/printserver.pid` state and
  terminate an exact `/home/cygnus/marlindriver/print_service.py` process if it
  survived the stock init stop path.
  The installer no longer rewrites the stock coordinator's Python
  command-writer module; native route selection and print-control cleanup now
  stay in Deneb-owned shell/C code while stock Python files remain untouched
  backup artifacts. Generated Deneb printserver/coordinator init shims also
  avoid spelling Python entry points or Python runtime environment exports
  directly; the generated printserver shim no longer delegates back to the
  stock driver.
- Pending-job file ownership tightened further: default queued-job JSON-array
  reads and missing-file cleanup are now wrapped by
  `common/print/pending_job_file.*`, so Deneb/Cura REST surfaces no longer
  carry the pending metadata path or cleanup race behavior locally.
- Deneb packages now include `/usr/bin/deneb-printsvc-smoke`, a no-Python
  device-side smoke/resource harness for the native print-service milestone.
  Its default mode only records route/status/process/resource snapshots; heat,
  boot/backend readiness, observe-only client API/bridge proof, motion,
  macro-backed manual actions, native-route assertion, REST multipart job
  upload/abort, explicit preheat abort,
  explicit active-print abort with a configurable delay, Cura cluster API job upload/abort, pause/resume,
  short-job completion, and native
  service-restart recovery phases require explicit lab flags and are intended to
  generate the live evidence required by Section 8 once
  SSH/hardware validation is allowed again. The harness writes a full log and a
  compact summary with phase return codes, bounded boot-sync ready timing, the
  selected print-backend route body, scalar `/printer/status` values plus
  sanitized full status bodies, and `/printer` root body for every snapshot,
  `/proc`-sourced process VmSize/VmRSS samples, system CPU jiffies, load
  averages, uptime samples for boot/ready timing correlation, and completed-job
  bytes/elapsed/bytes-per-second throughput records for stock and native print
  paths, including local/USB native job acceptance evidence. When
  `--client-proof` is enabled, it also records UM API, Cura cluster, and Digital
  Factory bridge status rows without heat or motion. The local release
  package also includes
  `deneb-printsvc-smoke-verify`, a shell-only summary verifier for
  observe/native/idle/boot-sync/client-proof/heat/motion/macro/local-job/REST-job/preheat-abort/
  active-abort/Cura-job/pause-resume/completion/restart evidence, including native `deneb-printsvc`
  process ownership with no running `print_service.py`, explicit initial idle
  status and inactive stop-state evidence, route diagnostics and
  captured status bodies that report `native_only_route:true` while keeping
  summary `status=` values scalar, rejection of any
  native process sample that shows stock `print_service.py` returned,
  local/USB job evidence tied to that native ownership and emitted by the
  native CLI as accepted `pre_print` active/stop-allowed state followed by
  aborted `idle` inactive/stop-disabled state, active-job status transitions from
  `printing` to `paused` and back to `idle`, including active-print abort and
  natural-completion snapshots that either show active `printing` before
  settling or a fast-completed idle snapshot with zero completion-wait elapsed
  time, native
  active/stop-allowed flags during preheat and active jobs, heat/motion/macro status-root snapshot
  evidence, observe-only UM API/Cura cluster/Digital Factory bridge proof, and
  resource/throughput evidence that includes native `deneb-printsvc` driver RSS
  when native mode is required, so live runs can be checked on target without
  Python. Packages also include
  `deneb-printsvc-smoke-compare`, a shell-only stock/native summary comparator
  that emits before/after deltas for system memory, driver-process RSS, raw
  CPU jiffies, initial-to-final CPU jiffies, boot-sync elapsed time, and print throughput
  without exporting data to an external Python script. It also fails if the
  stock summary lacks initial/final `print_service.py` process evidence, CPU
  or throughput intervals are not positive, or the native summary lacks
  native-only route evidence in route diagnostics or any required status
  lifecycle body, lacks scalar boot-sync status plus native-only boot-sync
  status-body evidence, reports the wrong status value for a required lifecycle
  phase, contains a stock `print_service.py` process sample, lacks native
  local/USB IPC job acceptance plus accepted stop-state and abort/idle-state
  evidence, lacks explicit generic-job, Cura-cluster, preheat, or active-print
  abort-requested/draining evidence with Stop disabled during cleanup, lacks
  `deneb-printsvc` process ownership, or lacks native active/stop-allowed
  evidence in any required active or inactive printer-root lifecycle body. Its
  `--require-reduction` mode fails unless native system memory,
  driver-process RSS, CPU interval, and boot-sync elapsed time are lower than
  stock while native print throughput remains at least stock.
  The package also carries `deneb-printsvc-smoke-selftest`, a shell-only
  synthetic summary fixture runner that exercises the full verifier and
  comparator gates locally and invokes the live harness'
  `--summary-parser-selftest` mode so scalar status extraction is tested
  without hardware, including expected failures for missing initial idle,
  missing native
  stop-safety evidence, missing status-body native-route evidence in verifier
  and comparator paths, missing native-route evidence in a single comparator
  lifecycle status snapshot, missing active-abort or natural-completion active
  status evidence, missing generic-job or Cura-cluster
  abort-requested/draining status evidence, Cura-cluster abort-requested with
  Stop still enabled, Cura-cluster abort-draining that reports idle while
  native active remains true,
  a wrong single-phase lifecycle status value, boot-sync summaries that put the
  full status response into `status=` or omit `status_body` native-route proof,
  missing client-proof rows, missing active or inactive stop-safety evidence in a single comparator
  lifecycle snapshot, missing
  native local/USB job evidence, a non-native-only route diagnostic, a returned
  stock `print_service.py` process in a native run,
  missing stock
  `print_service.py` baseline evidence, missing native `deneb-printsvc`
  RSS evidence, zero-throughput records, and nonzero
  throughput regressions under strict reduction mode, so the
  evidence contract can be tested without Python or live hardware.
  `deneb-printsvc-cli-selftest` runs the actual native binary's `--smoke-test`
  and `--local-job-smoke` entry points against a temp G-code file without
  opening the motion serial device, then rejects missing accepted/aborted
  local-job stop-state rows.
  `deneb-printsvc-init-selftest`
  statically checks the packaged native init, installer source,
  installer-generated printserver heredoc, and installed printserver shim for
  native ownership markers, exact stock
  `/home/cygnus/marlindriver/print_service.py` cleanup, stale
  `/var/run/printserver.pid` cleanup, correct startup/stop cleanup ordering,
  and absence of Python driver launch commands.
  The local release
  package build was inspected and contains `deneb-printsvc`, `deneb-printsvc-smoke`,
  `deneb-printsvc-smoke-verify`, `deneb-printsvc-smoke-compare`,
  `deneb-printsvc-smoke-selftest`, `deneb-printsvc-cli-selftest`,
  `deneb-printsvc-init-selftest`, `deneb-printsvc-release-gate-selftest`,
  `deneb-printsvc-native-audit`,
  `deneb-printsvc-native-audit-selftest`,
  `manifest.txt`, and the declared
  `LVGL_LICENSE_TLSF.txt` notice, with no packaged Python or `print_service.py`
  entries. The package builder fails closed if a Python driver artifact appears
  in the staging directory or final `.deneb` archive, and the PowerShell release
  builder now inspects the final `.deneb` archive for the same
  no-Python-driver invariant while requiring `deneb-printsvc`, the smoke,
  CLI, init, release-gate shell selftests, `deneb-printsvc-native-audit`, and
  `deneb-printsvc-native-audit-selftest` to be present.
  The package builder and release verifier run the shell-only smoke/init/native
  audit gates plus negative audit fixtures for source-route regressions,
  stock Python launchers, package audit wiring, installer audit-selftest
  wiring, missing smoke/CLI/init selftest artifacts, missing declared TLSF
  notice, packaged Python artifacts, and manifest-gate loss; none execute the
  cross-compiled target binary before accepting the artifact. The static audit
  also checks that both shell and PowerShell release entry points keep the
  non-experimental stock/native live-summary gate wired to full native smoke
  verification plus strict resource-reduction comparison, and
  `deneb-printsvc-release-gate-selftest` behaviorally covers invalid channels
  plus missing or malformed stable/nightly live-summary inputs while using an
  isolated package-version override so expected-failure runs cannot disturb the
  active package staging. The live smoke harness now also generates a bounded
  old-Marlin Z movement completion fixture with
  `--make-complete-fixture` and rejects dwell/M400-only completion fixtures
  before upload, keeping local completion-test tooling from confusing firmware
  wait commands, minimum-Z safety trips, or temperature polling loops with real
  stream completion evidence. It now also generates bounded Z-only active-job
  fixtures and low-temperature preheat-abort fixtures, so active-abort,
  pause/resume, Cura, and preheat smoke evidence can use fresh known inputs
  instead of stale device-side files. The generated Z fixture cap is 480
  relative `G1 Z-0.20 F30` moves, keeping total travel to 96 mm away from homed
  Z max while providing enough runtime for pause/resume, Cura-running,
  completion, and sequence-wrap smoke evidence. Packages default
  to `DENEB_RELEASE_CHANNEL=experimental`; `nightly` and `stable` package
  builds now require live stock/native smoke summary paths in
  `DENEB_PRINTSVC_STOCK_SUMMARY` and `DENEB_PRINTSVC_NATIVE_SUMMARY`, then run
  `deneb-printsvc-smoke-verify --full` and
  `deneb-printsvc-smoke-compare --require-reduction` before the archive is
  created. The PowerShell release entry point exposes the same boundary through
  `-ReleaseChannel`, `-PrintsvcStockSummary`, and
  `-PrintsvcNativeSummary`, and fails before building if a non-experimental
  channel is requested without both live summaries. The manifest records the
  experimental native-printsvc status and the non-experimental evidence gate so
  the release artifact carries the same boundary; the installer preserves the
  release-gate evidence selftest for auditability, and both the shell package
  builder and PowerShell wrapper inspect the archived manifest for those fields
  before accepting the artifact. The printsvc CTest suite registers the smoke,
  CLI, init, native-route audit, and native-audit negative-fixture selftests when
  `sh` is available, and the installer deploys them to `/usr/bin` for
  target-side gate checks. The
  installer rejects update packages containing Python driver artifacts, runs
  the packaged native audit over `/tmp/update`, rejects manifests that lack the
  native-printsvc experimental status or non-experimental evidence gate,
  preserves the accepted manifest at `/etc/deneb/manifest.txt`, and runs the
  installed print-service smoke-tool, CLI, native-audit selftest, and
  init-handoff selftests before completing the update. The installed init
  selftest is target-aware: when package/source paths are absent, it validates
  `/etc/init.d/deneb-printsvc` plus the generated `/etc/init.d/printserver`
  shim. The installed release-gate selftest is also target-aware: it validates
  `/etc/deneb/manifest.txt` and the installed smoke verifier, comparator, audit,
  and audit selftest instead of requiring source-only `ui/build-package.sh`.
- June 9, 2026 live observe-only native evidence from
  `dist/Deneb_Update_8816c0b.deneb`: the package installed over SSH, rebooted,
  and passed installed CLI, init-handoff, release-gate, and native-audit
  selftests under `set -e`. A subsequent
  `/usr/bin/deneb-printsvc-smoke --native --restart --boot-sync` run verified
  native-only route, boot-sync readiness, idle status, service restart recovery,
  `native_active:false`, `native_stop_allowed:false`, and
  `print_service_py=0`. The `/printer` snapshots reported nonzero ambient
  readings instead of the earlier impossible `0/0 C`: bed current was about
  25.5 C with target 0.0 C, and nozzle current was about 28.3 C with target
  0.0 C. Process RSS samples contained the real `/usr/bin/deneb-printsvc`,
  `/usr/bin/deneb-api`, and `/usr/bin/deneb-ui` binaries after the smoke harness
  sampler was tightened to ignore its own shell wrapper. This evidence is
  observe-only; it does not close physical heat, motion, job, Cura, pause/resume,
  completion, active-abort, preheat-abort, or stock/native throughput gates.
- June 9, 2026 live observe-only client API/bridge evidence from
  `dist/Deneb_Update_fa29a67.deneb`: the package installed over SSH and
  `/usr/bin/deneb-printsvc-smoke --native --boot-sync --client-proof` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --boot-sync
  --client-proof`. The accepted summary proved native-only route, boot-sync
  readiness, UM API `/printer/status`, `/printer`, and `/system`, Cura cluster
  `/printers`, `/print_jobs`, and `/materials`, installed Digital Factory
  bridge status (`status_timeout` while offline), no stock `print_service.py`,
  nonzero ambient native temperatures around 30.3 C bed and 32.8 C nozzle, and
  final process RSS samples for `/usr/bin/deneb-printsvc`,
  `/usr/bin/deneb-api`, and `/usr/bin/deneb-ui`. The optional UM `/print_job`
  probe returned rc 8 and is intentionally recorded as `optional_phase`, so it
  does not satisfy or fail the required client proof. This closes only
  observe-only client-surface evidence; LCD/Web UI workflows, Cura
  upload/start/abort, Digital Factory job lifecycle, physical phases, and
  stock/native resource comparison remain open.
- June 9, 2026 installed `dist/Deneb_Update_be6a5b7.deneb` and verified the
  packaged safe fixture generators on-device. The installed observe-only smoke
  again passed native restart/boot-sync with nonzero ambient temperatures and no
  stock Python driver. A low-temperature preheat-abort run showed `printing`
  with bed/nozzle targets 35/45 C, `native_active:true`,
  `native_stop_allowed:true`, then final idle with both native flags false and
  targets back to 0. A bounded Z-only active-abort run showed `printing` with
  Stop allowed, API abort success, and final idle with Z at 202.6 after homed
  207.0. These are first physical abort proofs for generated fixtures, not the
  full stock/native resource or representative print matrix.
- June 9, 2026 installed `dist/Deneb_Update_d0b61f7.deneb` and verified a
  corrected bounded native pause/resume/abort smoke on hardware. The live run
  used all-axis prehome because pause/resume moves X/Y, and the active fixture
  kept XYZ in absolute mode so the smoke matched Cura-style jobs instead of
  leaving the firmware in `G91`. The accepted summary proved native route
  ownership, no stock Python driver, `printing` and `paused` with Stop allowed,
  resumed `printing`, `aborting` with Stop disabled during cleanup, and final
  `idle` with `native_active:false`, `native_stop_allowed:false`, live ambient
  temperatures, and no firmware error. This closes only the bounded generated
  pause/resume smoke slice; representative Cura geometry and stock/native
  resource comparison remain required before promotion beyond experimental.
- June 9, 2026 installed `dist/Deneb_Update_f83c1a1.deneb` after the native
  pause-position freshness fix. The target manifest reported `version:
  f83c1a1`; installed CLI, init-handoff, release-gate, native-audit,
  integration-audit, and integration-audit selftests passed; and
  `/usr/bin/deneb-printsvc-smoke --native --boot-sync --client-proof` passed
  `/usr/bin/deneb-printsvc-smoke-verify --native --idle --boot-sync
  --client-proof`. The accepted summary proved native-only route, idle native
  active/Stop flags false, no stock Python driver, UM API and Cura cluster
  client surfaces, installed Digital Factory bridge status, nonzero ambient
  telemetry near 29.7 C bed and 32.7 C nozzle, and final
  `/usr/bin/deneb-printsvc` RSS around 1576 KB. This is a current-build
  observe-only/client-surface refresh; the fresh-M114 pause fix still needs a
  supervised all-axis pause/resume motion run, and Section 8 still needs
  representative Cura geometry plus strict stock/native resource comparison.
- June 9, 2026 installed `dist/Deneb_Update_af12aaf.deneb` after moving LCD
  abort-display context into shared `status_state.*` helpers. The target
  manifest reported `version: af12aaf`; installer smoke, CLI, and native-audit
  selftests passed; and `/usr/bin/deneb-printsvc-smoke --native --boot-sync
  --client-proof` passed `/usr/bin/deneb-printsvc-smoke-verify --native --idle
  --boot-sync --client-proof`. The accepted summary proved native-only route,
  no stock Python driver, idle native active/Stop flags false, UM API and Cura
  cluster client surfaces, installed Digital Factory bridge status, nonzero
  ambient telemetry near 29.9 C bed and 32.8 C nozzle, and final
  `/usr/bin/deneb-printsvc` RSS around 1584 KB. This refreshes current-build
  observe-only evidence only; LCD hardware abort-flow, representative Cura
  geometry, and strict stock/native resource comparison remain open.
- Shared print-state code has been split further by responsibility:
  `common/print/print_state_rules.*` owns lifecycle/status/context decisions,
  `common/print/print_action_rules.*` owns REST/Cura action parsing and action
  plans, and `common/print/print_string.*` owns small reusable ASCII matching
  helpers used by both. This keeps the de-python rewrite from growing a single
  catch-all state file while moving touchscreen, web/API, and native
  `deneb-printsvc` callers toward one shared contract. The field-level context
  helpers in `print_state_rules.*` also reduce LCD and web/API backend ZMQ
  clients from duplicating observation setup before computing active,
  preparing, and stoppable print-context flags. `status_payload.*` also owns
  field-level filename context construction now, so those clients share the
  same native helper before resolving transient macro names, pending-job names,
  or retained print filenames. Print elapsed-time, percentage, fraction, and
  normalization math now lives in `common/print/print_timing_rules.*`, keeping
  timing behavior separate from lifecycle/context classification.
- The patched-client integration audit now lives in
  [PRINTSVC_INTEGRATION_AUDIT.md](PRINTSVC_INTEGRATION_AUDIT.md). It records the
  native owner, compatibility boundary, removal condition, evidence, and
  remaining proof for LCD `backend_comm`, web `backend_zmq`, REST/Cura API
  paths, conflict/preheat bridges, pending-job metadata, direct macro/G-code
  calls, status classification, diagnostics, and native print-service callers.
  Each row now also carries a placement decision: client via shared helpers,
  shared library/API boundary, or native service-owned. The shell-only
  `deneb-printsvc-integration-audit` and selftest are packaged and wired into
  CTest, package build, archive audit, and installer validation so new
  patchwork has to declare an owner and placement before it ships.
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
