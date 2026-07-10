# Target Automation Evidence — 2026-07-10

Target: UltiMaker 2+ Connect at DHCP address `10.10.10.246` during this run.

Installed package: `1a4a4afe-dirty` (older than the current `main` source).

This is dated evidence for the installed package only. It does not prove later
source changes, and it does not replace hands-on UI or long-soak testing.

## Safety contract

- Never command axis motion before homing.
- Treat any limit/endstop alert as an immediate stop condition.
- Commanded test positions must stay within X/Y/Z `0..190 mm`.
- Bed target must not exceed 50 C; nozzle target must not exceed 195 C.
- Reboot is permitted when the printer is idle and heater targets are zero.

Every physical run was guarded by state, heater-target, and new-alert checks.
No limit, endstop, thermal, or controller fault alert occurred. Homing and the
firmware's completion cleanup can report physical coordinates beyond the
commanded test envelope; no fixture `G0`/`G1` position exceeded 190 mm.

## Results

| Test | Result | Evidence boundary |
| --- | --- | --- |
| Clean reboot and idle recovery | **PASS** | One `deneb-api`, idle, empty queue, zero heater targets, no Python executable process. |
| Installed native self-tests | **PASS** | CLI, init, release-gate, native-audit, and integration-audit self-tests all returned 0. These are fixture/static checks, not physical proof. |
| All-axis home | **PASS** | Installed macro `G28; G0 X105 Y0 F9000` completed without alerts. |
| Bounded generated completion | **PASS** | Fixture used X/Y `30..160`, Z `175..190`, no extrusion or heat. Queue cleared and cleanup completed without alerts. |
| REST active abort | **PASS** | Pre-homed bounded job transitioned `printing -> aborting -> idle`; queue cleared and heater targets stayed zero. |
| Low-temperature heat lifecycle | **PASS** | Targets were bed 40 C/nozzle 50 C, then zero. Maximum observed currents were bed 29.7 C/nozzle 55.3 C; no alert. |
| Cura cluster upload/start/abort | **PASS** | Generated bounded job started through cluster API, then aborted to idle with empty queue and zero targets. This is API compatibility proof, not desktop Cura UX proof. |
| Preheat cancel-before-heat | **PASS, narrow** | Abort occurred at job line 0 with both targets still zero. This does not prove abort while a heater is actively targeting. |
| Pause/resume | **NOT RUN — SAFETY BLOCKED** | Installed pause policy commands `G0 Z205 F9000`, outside the authorized 0..190 mm motion envelope. No waiver was assumed. |
| Material load/unload | **NOT RUN — CONTEXT BLOCKED** | Macros command extruder motion and require known loaded-material/nozzle context that automation could not safely establish. |
| Diagnostics export | **NOT AVAILABLE** | Log API worked, but no diagnostics-export implementation was installed. |

## Status and lifecycle defects observed

1. Manual heat reported printer `status=printing` / native-active even with no
   print job. The heater behavior passed, but the status is semantically false.
2. Restarting `deneb-api` while `deneb-web` was active produced two API
   processes. A clean reboot restored one. The init ownership/fallback policy
   needs a single authoritative owner.
3. The runtime-inventory script searches full command-line substrings and can
   falsely report a shell command containing the word `python`. A strict
   `/proc/<pid>/exe` check found no Python executable process.
4. `/api/v1/printer` reported firmware and machine identity fields as `none`,
   with an invalid/missing PCB identifier.

## Web/API resource and concurrency results

After a clean reboot, the relevant idle RSS samples were approximately:

| Process | RSS |
| --- | ---: |
| `deneb-ui` | 1.6 MiB |
| `deneb-api` | 0.9–1.0 MiB |
| `deneb-printsvc` | 1.0 MiB |
| `lighttpd` | 0.44 MiB |
| `deneb-mdns` | 0.12 MiB |

Three simultaneous SSE clients plus 120 REST requests passed with zero request
failures. `deneb-api` stayed at 1,032 KiB RSS; its descriptor count increased
from 18 to a peak of 25 and settled at 21.

Four simultaneous SSE clients plus REST polling did not drain within the
90-second harness bound. After clients were gone, the API retained seven extra
proxy/Unix sockets (`14 -> 21` descriptors), and lighttpd-side connections were
visible in `CLOSE_WAIT`. RSS did not grow, but connection capacity was starved
and descriptors did not return to baseline. Treat this as a release-blocking
concurrency/cleanup defect, not a successful load test.

An approved clean reboot restored one API process, 876 KiB RSS, 14 descriptors,
idle state, empty queue, zero heater targets, and no Python executable process.

## Build and deployment boundary

Current source was not deployed. The registered Debian WSL environment on the
Windows workstation failed with
`Wsl/EnumerateDistros/Service/E_ACCESSDENIED`, so a current MIPS package could
not be built. See [WSL Build Environment](../WSL_BUILD_ENVIRONMENT.md).

Until a current package is built and installed, the one-in-flight Pause change
and other post-`1a4a4afe` fixes remain host/source claims only.
