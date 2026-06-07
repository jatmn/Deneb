# Deneb Backend IPC Protocol

## Architecture

ZeroMQ (ZMQ) over localhost TCP between the stock Python backend services and
Deneb's native UI/API clients.

## Port Map

### Print Service (marlindriver/print_service.py, PID 1124)
- tcp://127.0.0.1:5555 - ZMQ PUB (publishes status, topic "10001")
- tcp://127.0.0.1:5556 - ZMQ REP (receives commands)

### Coordinator (coordinator/coordinator.py, PID 1129)
- tcp://127.0.0.1:5565 - ZMQ PUB (republishes aggregated status)
- tcp://127.0.0.1:5566 - ZMQ REP (receives commands from menu/UI, proxies to print service)

### Deneb UI and Web/API clients
- tcp://127.0.0.1:5565 - ZMQ SUB (status from coordinator)
- tcp://127.0.0.1:5566 - ZMQ REQ (commands to coordinator)

## Data Flow

  print_service --[PUB 5555]--> coordinator --[PUB 5565]--> deneb-ui / deneb-api
  deneb-ui / deneb-api --[REQ 5566]--> coordinator --[REQ 5556]--> print_service

## Status Protocol (SUB on port 5565)

Subscribe to topic: "10001"
Frame format: "10001<{json_payload}"

### JSON Fields
- file: current file name (string, "none" when idle)
- headTset: nozzle target temp (float)
- headTcur: nozzle current temp (float)
- bedTset: bed target temp (float)
- bedTcur: bed current temp (float)
- X, Y, Z, E: position (float)
- uuid: print job UUID (string)
- source: print source (string)
- Ttot: total time estimate (int, seconds)
- Tleft: time remaining (int, seconds)
- req: current request type (string, e.g. "Paused", "Resume")
- received_faults: fault list from Marlin (array)

## Command Protocol (REQ on port 5566)

Frame format: "COMMAND<json_payload"

### Commands
- GCODE<["M140 S70"]                    - Send raw G-code
- GCODE<["G28 X Y", "M18"]              - Multiple G-code commands
- MACRO<{"macro":"init.gcode"}           - Execute macro from /home/cygnus/marlindriver/gcode/
- JOB<{"file":"/mnt/sda1/file.gcode","source":"USB","uuid":"0"} - Start print
- ABORT<{}                               - Abort current print
- PAUSE<{}                               - Pause print
- RESUME<{}                              - Resume print

### Available Macro Files
/home/cygnus/marlindriver/gcode/
- init.gcode                    - Printer initialization
- home_and_center_head.gcode    - Home all axes and center head
- home_release.gcode            - Home and release motors
- print_finish.gcode            - Post-print finalization
- material_down.gcode           - Unload material
- move_material_up.gcode        - Load material
- move_material_finish.gcode    - Finish material operation
- retract.gcode                 - Retract filament
- move_buildplate_up.gcode      - Move buildplate up
- move_buildplate_down.gcode    - Move buildplate down
- buildplate_level_step1-4.gcode - Bed leveling steps
- buildplate_level_finish.gcode - Finish bed leveling
- framelight_on.gcode           - Turn frame light on
- framelight_off.gcode          - Turn frame light off

## Menu Communication Architecture (from executor.py)

The stock menu uses TWO communication paths:
1. Raw ZMQ REQ on port 5566 - for GCODE/MACRO/JOB/ABORT/PAUSE/RESUME
2. Gershwin IPC - for higher-level coordinator calls (prefixed with ::, ??, __)

For our LVGL UI, we only need path #1 (raw ZMQ commands on port 5566).
The coordinator proxies these to the print service on port 5556.

## Deneb Compatibility Layers

Current Deneb clients of this coordinator IPC include:

- `ui/src/backend_comm.c` for the native touchscreen UI.
- `web/src/backend_zmq.c` for `deneb-api`.
- `web/src/api_print_job.c` for UM API v1-shaped upload/start/state handling.
- `web/src/api_cluster.c` for Cura local cluster API discovery, materials,
  upload/start, pending-job visibility, and pause/resume/abort/print actions.

The Cura cluster compatibility path also stores temporary pending-job metadata
at `/tmp/deneb-cluster-print-job.json` so Cura-started jobs remain visible while
Deneb validates metadata, waits for conflict confirmation, prepares, and
preheats.

When `deneb.printsvc.enabled=1`, `ui/src/backend_comm.c` and
`web/src/backend_zmq.c` select native `deneb-printsvc` directly on status
`5555` and command `5556` for print-service traffic. The stock coordinator
route remains the default when the lab gate is `0`, preserving coordinator,
Digital Factory, and rollback assumptions during the first replacement stage.
`common/print/print_backend_route.*` owns this route decision and endpoint
mapping for Deneb clients. `DENEB_PRINTSVC_BACKEND=native` or `coordinator` can
override the route for host/lab debugging.

Upload registration, conflict continue/cancel, and pending-job cancel now use
native Deneb code paths. Pending-job file handling, command-frame formatting,
print-state classification, and web/API status labels are shared native helpers
so touchscreen, web/API, and `deneb-printsvc` clients agree on escaping,
preheat, paused, abort, active-print, offline, and finished-job state.
Touchscreen/web macro, multi-line G-code, and job-start callers now use backend
helper functions that own stock frame formatting. The native `deneb-printsvc`
milestone should keep collapsing remaining direct clients toward shared Deneb
print-control helpers or a single native service API.
