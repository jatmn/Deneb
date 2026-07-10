# Third-Party Slicer Compatibility

Last reconciled: 2026-07-10

This is the user-facing slicer contract for Deneb. The detailed controller-source
evidence is in
[MARLIN_COMMAND_PROTOCOL_AUDIT.md](evidence/MARLIN_COMMAND_PROTOCOL_AUDIT.md).

## Current support

| Slicer path | Status |
| --- | --- |
| Cura 5.13 with the Deneb plugin and stock UltiMaker 2+ Connect definition | **Target-proven happy path; broader failures remain** |
| PrusaSlicer, OrcaSlicer, SuperSlicer, and generic custom profiles | **Not yet released or target-proven** |
| Direct USB/UART streaming to the motion controller | **Unsupported and unsafe** |

The Cura plugin maps Deneb to Cura's maintained
ultimaker2_plus_connect definition. That is the current reference for machine
geometry, nozzle variants, material profiles, and generated print metadata.

## Why generic Marlin profiles are unsafe

The Connect controller is not standard modern Marlin. It uses a framed
sequence/CRC serial transport and private command meanings. In particular:

| Code | Connect/Deneb meaning | Generic modern-Marlin risk |
| --- | --- | --- |
| M401 | Quick-stop and stopped-position report | Modern Marlin uses it to deploy a probe |
| M290 | Controller power budget | Modern Marlin uses it for babystepping |
| M405-M407 | Connect flow-sensor control/readout | Modern Marlin uses them for filament-width sensing |
| M142 w... | Frame light | Connect requires lowercase w |
| M12030 | Top-cap fan | Private Connect command |
| M109/M190 | Deneb accepts and host-rewrites heater waits | Connect controller has no native handlers |
| G280 | Deneb/legacy prime marker | Not a controller G-code |
| G10/G11 | Not implemented by public Connect source | Firmware-retract output is unsafe to assume |

These differences matter both before and after the modern Marlin port. Profiles
must be versioned against the Deneb host and controller protocol versions.

## Draft configuration for unsupported slicers

This is a compatibility-development starting point, not a release-ready profile:

1. Start from the slicer's UltiMaker 2+ Connect definition when one exists.
   Otherwise copy physical geometry, nozzle, material, acceleration, and speed
   limits from the matching Cura definition; do not guess them.
2. Select file-based Marlin G-code output, but upload the completed file through
   Deneb. Never connect the slicer directly to the controller UART.
3. Use slicer-generated extrusion moves rather than firmware retract G10/G11.
4. M104, M140, M109, M190, M106, M107, M82, M83, M400, G0/G1, G2/G3, G90,
   G91, and G92 are within the current intended Deneb boundary. This is not yet
   a complete allowlist or third-party-profile proof.
5. Do not emit M290, M401, M405, M406, M407, M998, or M12000-series commands
   from a generic printer profile.
6. Do not add lighting, top-cap, homing, probing, or controller-configuration
   commands to ordinary print-job start/end G-code.
7. Keep all coordinates and temperatures within the selected machine/material
   profile. Deneb's automation safety envelope is not a substitute for slicer
   machine limits.

Deneb currently owns prepare, homing, modal startup, heater-wait translation,
prime handling, pause/resume, abort, and finish cleanup. Third-party start/end
templates must not duplicate those sequences until the profile conformance
fixtures prove the combined behavior.

## Required work before calling a slicer supported

- Export and version the actual profile and reviewed start/end templates.
- Generate representative jobs covering layers, arcs, retracts, travel, support,
  cooling, heating, pauses, completion, and abort.
- Add a preflight validator that classifies every command as pass-through,
  rewritten, Deneb-only, controller-private, unsupported, or
  unsafe/conflicting.
- Reject unknown or unsafe commands before any heat or motion.
- Run host conformance first, then target testing under the physical-machine
  safety contract.
- Publish the Deneb version, controller-firmware version, slicer version, and
  profile hash used for each accepted profile.

## Known source discrepancy

Current native prime paths emit G10, while public Connect source removed G10 and
G11. Existing Cura happy-path target evidence does not prove those individual
commands behaved as intended. Resolve that discrepancy before publishing
third-party profiles or treating the command table as a final allowlist.
