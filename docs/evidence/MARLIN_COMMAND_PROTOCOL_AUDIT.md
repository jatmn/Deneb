# Marlin Command And Protocol Audit

Date: 2026-07-10

This is a source audit, not build or controller-flash proof. It defines the
compatibility boundary for a modern controller port and third-party slicers.

## Sources

| Source | Revision |
| --- | --- |
| UltimakerMarlin Ultimaker2 parent | 8698fb6ad434 |
| UltimakerMarlin Ultimaker2+Connect | acb22046c69a |
| Marlin 2.1.2.8 | 1cd56c4ccd48 |
| Deneb main worktree | reviewed 2026-07-10 |

The Connect branch is three commits ahead of its Ultimaker2 parent. It is a
controller and host-protocol port, not a configuration-only variant.

## Added and modified commands

Connect adds no G-code numbers. It implements G0, G1, G2, G3, G28, G90, G91,
and G92, while removing the parent's G4, G10, and G11 handlers.

| Connect-added M-code | Meaning | Modern Marlin status |
| --- | --- | --- |
| M142 w0..255 | Frame-light PWM; lowercase w | Absent |
| M290 P... I... | Total/idle power budget | **Conflict:** babystepping |
| M291 R... A... V... | Bed electrical model | Absent |
| M292 T... R... V... P... | Hotend electrical model/power | Absent |
| M405 S... A... | Connect flow sensor enable/config | **Conflict:** filament-width control |
| M406 | Reinitialize Connect flow algorithm | **Conflict:** disable filament-width control |
| M407 S... | Raw Connect flow-sensor read | **Conflict:** report filament diameter |
| M998 | Enter stopped/error state | Absent |
| M12000, M12001 | Set build-volume maximum/minimum | Absent |
| M12003 | Report controller-board type | Absent |
| M12004 | Set axis/extruder directions | Absent |
| M12005, M12006 | Hotend/bed temperature compensation | Absent |
| M12010 | Read/write TMC2130 registers | Absent |
| M12020 | High-power 24 V safety test | Absent |
| M12030 | Top-cap fan fixed/automatic mode | Absent |
| M12031, M12032 | Top-cap presence/temperature | Absent |

Existing numbers with Connect-specific contracts also need porting:

| Code | Required compatibility |
| --- | --- |
| M105 | Temperature, heater-output, flow, bed, and top-cap response fields |
| M114 | Position response used after pause/abort |
| M115 | Machine type, PCB ID, and build identity |
| M119 | Unambiguous endstop names and hit/open status |
| M301/M304 | Feed-forward, integral cap, range, compensation, and debug parameters |
| M401 | **Conflict:** Connect quick-stops and reports XYZ/E; modern M401 deploys a probe |
| G28 | Broken/stuck endstop checks, home-distance output, and stop reasons |
| G92 | Flow-sensor extrusion-position synchronization |

Modern M410 quick-stops, but does not by itself preserve the Connect M401
response contract. Connect also has no M109 or M190; Deneb translates these to
non-blocking targets plus host-side waits.

## Third-party slicer contract

A generic modern-Marlin profile is not safe for Deneb. Slicers must submit
through Deneb, which owns controller packet framing, wait translation, planner
backpressure, status polling, and fault handling.

| Profile concern | Required rule |
| --- | --- |
| Firmware flavor | Use a versioned Deneb profile, not generic Marlin 2 |
| Submission | Upload through Deneb; never stream directly to the controller UART |
| Command set | Preflight against a tested allowlist; reject unknown/conflicting codes |
| M109/M190 | Accepted by Deneb and host-rewritten for the current controller |
| G280 | Host-side prime marker, not a controller command; expansion needs proof |
| G10/G11 | Do not assume firmware retract support |
| M401 | On Connect this aborts motion; it is not a probe-deploy command |
| M290/M405-M407 | Never emit generic modern-Marlin meanings |
| M142/M12030 | Deneb machine controls, not ordinary job commands |
| Start/end G-code | Ship reviewed templates for each supported slicer/version |

Before claiming Cura, PrusaSlicer, OrcaSlicer, or other support, provide exported
profiles, representative generated-file fixtures, and a preflight validator.
Classify every code as pass-through, rewritten, Deneb-only, controller-private,
unsupported, or unsafe/conflicting. Version the guide against both host and
controller protocol versions.

## Serial protocol is part of the port

Connect does not accept ordinary line-oriented Marlin input:

- host packets begin ff ff, then sequence, payload length, ASCII payload, and
  CRC16-CCITT;
- controller responses are CRC8-protected oSSPPCC, rSSPPCC, and qSSPPCC lines;
- responses acknowledge/reject execution and advertise planner capacity;
- faults and output may arrive asynchronously before acknowledgement.

A modern build without either this layer or a coordinated modern-protocol host
adapter is incompatible with current printsvc.

## Current Deneb dependencies and defects

Deneb directly uses M401 quick-stop, M142 lighting, M12030 top-cap fan control,
M105/M114/M115 status polling, and the Connect packet/ACK/resend protocol.

This audit exposed two source mismatches:

1. framelight_on.gcode and framelight_off.gcode send M142 R..., but Connect reads
   only lowercase w. Physical behavior of these stale macros is unproven.
2. Native startup and G280 prime paths emit G10, but public Connect source
   removed G10/G11. Treat those prime operations as unverified or incorrect
   until traced.

## Connect versus modern Marlin protocol

The modern release has not simply preserved the old protocol unchanged.

| Aspect | Connect fork | Marlin 2.1.2.8 | Initial assessment |
| --- | --- | --- | --- |
| Framing | ff ff header, explicit length, sequence 0-254 | Newline-delimited commands with optional N line number | Connect recovers framing more explicitly |
| Integrity | CRC16-CCITT over sequence, length, payload | 8-bit XOR checksum for numbered lines | Connect is stronger against multi-bit corruption |
| Resend | CRC8-protected reject naming expected sequence | Resend: N after line/checksum error; receive input is flushed | Modern is standard, but host rewind behavior needs testing |
| Acknowledgement | o response after handler plus planner-free count | ok after handler processing | Broadly equivalent execution boundary |
| Buffer reporting | Planner-free count and asynchronous q update | ADVANCED_OK reports planner P and command-buffer B free space | Modern exposes more queue information |
| Emergency handling | M401 is an ordinary queued custom handler | EMERGENCY_PARSER recognizes M108, M112, M410, M524, and M876 while queued work is blocked | Modern is potentially safer and lower latency |
| Busy/long operation | Custom asynchronous output and host-owned waits | HOST_KEEPALIVE and busy states are available | Modern is better standardized |
| Parser | Old command switch with character searches | Structured current Marlin parser and command dispatch | Modern is easier to maintain and validate |
| Ecosystem | Private Deneb/Griffin transport | Common host tooling and documented Marlin behavior | Modern sharply reduces custom integration |
| AVR cost | Already fitted to the Connect image | ADVANCED_OK, emergency parser, buffers, and drivers consume flash/RAM | Must be measured on the actual Mega 2560 build |

Recommended first prototype: standard numbered modern-Marlin lines with checksums,
ADVANCED_OK, EMERGENCY_PARSER, and a bounded Deneb send window. Do not enable
binary file transfer, MeatPack, XON/XOFF, or larger buffers without a measured
need.

This recommendation is conditional. The protocol suite must inject single- and
multi-bit corruption, missing newlines, duplicate/lost commands and responses,
RX overflow, controller reset, resend during a full window, and line-number
resynchronization with M110. It must also measure M410 latency and stopped
position under queued motion, P/B accuracy, planner starvation, asynchronous
fault handling, and recovery after a bad frame.

If standard Marlin fails an accepted integrity or recovery gate, add only the
smallest isolated transport feature required. Do not copy the entire Connect
serial stack by default.

## Minimal-custom-Marlin architecture

Use two deliberately small host components rather than a general G-code
interpreter:

1. A file preflight/normalizer scans the complete slicer output before the job
   can heat or move. It rejects conflicts and expands only deterministic,
   versioned rewrites.
2. A runtime controller adapter in printsvc translates command/response and
   transport differences for the selected controller protocol. It owns sequence,
   resend, buffer accounting, wait orchestration, and response normalization.

The preferred modern lane is Deneb-native serial framing on the legacy
controller and standard modern-Marlin line/checksum/resend with ADVANCED_OK on
the modern controller. Marlin 2.1.2.8 ADVANCED_OK reports planner and command
buffer capacity, so the old Connect framing should be retained only if tests
show the modern protocol cannot meet corruption, recovery, or throughput gates.

| Keep or translate in Deneb host | Must remain in controller firmware |
| --- | --- |
| Slicer allowlist and preflight | Thermal regulation and runaway protection |
| M109/M190 non-blocking target plus wait | Endstop sampling, homing safety, and hard limits |
| G4 as planner drain plus bounded host timer, if proven | Heater/power electrical models and PID |
| G10/G11 deterministic expansion or rejection | Stepper/TMC2130 control and direction |
| M115 identity normalization | Flow-sensor sampling and extrusion synchronization |
| Legacy/modern response normalization | Watchdog, emergency stop, and fault latching |
| Legacy packet versus modern ADVANCED_OK adapter | Top-cap sensors/fan and controller-connected lighting |
| Job rejection for conflicting private codes | Board detection and 24 V safety self-test |

M401 is a candidate host translation to modern M410 followed by M114. Modern
quickstop_stepper already synchronizes position from the steppers, but this
mapping is not accepted until tests prove prompt interruption, queue
cancellation, stopped-position accuracy, fault behavior, and recovery. If it
cannot be made equivalent, retain one thin compatibility handler rather than
forking the motion planner.

Prefer compile-time board configuration or a bounded settings structure over
porting provisioning-only M-codes verbatim. Keep any unavoidable additions in a
small, isolated Deneb compatibility module with upstream-style tests; do not
scatter them through core Marlin.

## Required gates

- Freeze machine-readable command, parameter, response, stop-reason, and serial
  fixtures from source plus safe controller traces.
- Resolve each modern command collision explicitly.
- Add slicer profiles, generated fixtures, and preflight validation.
- Port board detection, ADS101X thermals, power, compensation, TMC2130, flow,
  top-cap, watchdog, endstop, and fault behavior.
- Build old Connect reproducibly before using it as the behavioral oracle.
- Prove protocol/commands without motion before staged physical testing.

Status: **source inventory establishes the port and slicer-documentation
boundary; implementation, build proof, trace conformance, profile proof, and
target proof have not started.**
