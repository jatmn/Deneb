# Deneb Print Service

`deneb-printsvc` is the native C replacement track for the stock
`/home/cygnus/marlindriver/print_service.py` service.

The service is intentionally split into focused source units from the first
scaffold:

- `command.*` parses the stock `COMMAND<json>` request frames.
- `ipc_zmq.*` owns the first-stage ZMQ PUB/REP compatibility boundary:
  status PUB on `127.0.0.1:5555`, command REP on `127.0.0.1:5556`, and topic
  `10001`.
- `status.*` serializes stock-shaped status JSON.
- `status_parser.*` parses Marlin temperature, position, version, and fault
  lines.
- `serial_transport.*` opens and configures the Marlin serial device.
- `flow_control.*` tracks sequence numbers, in-flight packets, ACKs, rejects,
  and resend requests.
- `heater_wait.*` owns native preheat target tracking and readiness checks.
- `marlin_packet.*` owns sequence-numbered packet formatting and CRC.
- `crc.*` owns CRC helpers.
- `gcode_stream.*` streams G-code line by line without loading whole files.
- `macro_registry.*` resolves stock macro names under
  `/home/cygnus/marlindriver/gcode/`.
- `service.*` owns the print lifecycle state machine and command dispatch.

## Safety State

The binary is packaged into Deneb update releases, but the init script is
lab-gated by `deneb.printsvc.enabled=0` by default. Installing Deneb therefore
does not disable or replace stock `printserver` yet.

This scaffold is not complete enough to run unattended prints. It exists to
make the de-python work buildable and testable while the remaining planner and
firmware buffer semantics, firmware verification, richer finish/abort motion
policy, and live-device validation are implemented.

Abort handling is deliberately owned by `service.*`; the current native path
clears print state without issuing duplicate homing or unsafe XY cleanup moves.

## Host Build

```sh
cmake -S printsvc -B /tmp/deneb-printsvc-host -DBUILD_HOST_STUB=ON
cmake --build /tmp/deneb-printsvc-host
ctest --test-dir /tmp/deneb-printsvc-host --output-on-failure
```

The full release build also cross-compiles `deneb-printsvc` and packages
`deneb-printsvc` plus `deneb-printsvc.init` into the `.deneb` artifact:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-update-release.ps1
```
