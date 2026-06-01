# Deneb Backend IPC Protocol

## Architecture

ZeroMQ (ZMQ) over localhost TCP between three Python services and the UI.

## Port Map

### Print Service (marlindriver/print_service.py, PID 1124)
- tcp://127.0.0.1:5555 - ZMQ PUB (publishes status, topic "10001")
- tcp://127.0.0.1:5556 - ZMQ REP (receives commands)

### Coordinator (coordinator/coordinator.py, PID 1129)
- tcp://127.0.0.1:5565 - ZMQ PUB (republishes aggregated status)
- tcp://127.0.0.1:5566 - ZMQ REP (receives commands from menu/UI, proxies to print service)

### Menu/UI (REPLACED BY DENEB UI)
- tcp://127.0.0.1:5565 - ZMQ SUB (status from coordinator)
- tcp://127.0.0.1:5566 - ZMQ REQ (commands to coordinator)

## Data Flow

  print_service --[PUB 5555]--> coordinator --[PUB 5565]--> deneb-ui
  deneb-ui --[REQ 5566]--> coordinator --[REQ 5556]--> print_service

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
