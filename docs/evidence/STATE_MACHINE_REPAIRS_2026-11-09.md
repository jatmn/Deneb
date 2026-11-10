# State-machine repair evidence — 2026-11-09

Baseline: `18186404e5c8bb324b13ba788a2ec7aef2f1c103`.
Scope: the 49 first-party workflow/protocol boundaries identified in the
implicit-state audit. These are modeling boundaries, not 49 verified defects.
This record covers source changes and host fixtures. No printer was contacted,
deployed, heated, moved, flashed or rebooted.

## Repaired contracts

- UI and Web distinguish a successful native command ACK from a rejection,
  malformed frame or transport failure. Pending handoff and retained filenames
  advance only after acceptance.
- Native macros and manual sequences run through the existing bounded queue.
  Admission returns an operation ID; completion requires the final M400 ACK.
  Print/manual ownership, cancellation and fault precedence are enforced at
  the backend. Pause saves outstanding heater prerequisites and has bounded
  probe/parking waits; Resume restores those prerequisites.
- Material and leveling consume the operation result. Leveling no longer owns
  a detached motion worker, and Cancel submits ABORT. Material movement no
  longer completes from an estimated-duration timer.
- Faults stop host submission, prevent replay of old motion, retain ERROR and
  attempt one best-effort shutdown sequence. JOB and ABORT cannot clear a fault.
- Browser retries own one cancellable timer. API SSE overflow, disconnected
  peers and slow readers release their descriptors. API startup takes a socket
  ownership lock; Web init invokes the dedicated API service.
- Cloud backoff wakes for local commands; stale application status closes the
  transport for reconnection. The local bridge reports rejected/timed-out
  actions as failures instead of successful cached state.
- Update/reset share an inherited operation lock and observe child exit.
  Returning after a UI restart reports an unknown result honestly. Failed
  installation no longer triggers an unconditional EXIT-trap reboot.

The contracts and numeric operation states are documented in
[BACKEND_IPC_PROTOCOL.md](../BACKEND_IPC_PROTOCOL.md) and
[FAULT_POLICY.md](../FAULT_POLICY.md).

## Inventory disposition

**Repair** means a concrete transition defect was corrected in source; the
coverage section below defines the available proof. **Retained** means the
existing behavior was kept without an evidenced defect requiring a rewrite.
**Open limit** identifies a remaining design or target-evidence boundary;
it is not a claim that the corresponding workflow is fully resolved.

| ID | Boundary | Disposition |
| --- | --- | --- |
| P01 | Execution ownership | Repair: backend admission excludes concurrent print, manual and lifecycle work. |
| P02 | Job preparation | Repair: cold-bed/nozzle prerequisites survive Pause/Resume. Existing preparation stages retained. |
| P03 | Pause handshake | Repair: active-job guard, saved heater waiter, wrap-safe report comparison and probe/parking deadlines. Physical stop remains unproven. |
| P04 | Resume handshake | Repair: active-job guard, bounded restoration and saved preparation/in-file heater wait. Physical restoration remains unproven. |
| P05 | Abort cleanup | Repair: repeat ABORT is idempotent during cleanup; manual cancellation avoids print homing/parking; faults remain latched. |
| P06 | EOF/finish cleanup | Retained: existing EOF and cleanup ACK drain. No new claim of sensor-confirmed mechanical stability. |
| P07 | Heater waits/preheat | Repair: a heat wait cannot overwrite ERROR; paused waits are preserved. A universal thermal deadline is not added. |
| P08 | Manual G-code queue | Repair: monotonic operation token survives service restart without counter reuse; running/cancelling/terminal results, final barrier and progress timeout. |
| P09 | Macro execution | Repair: production dispatch admits the bounded queue and returns promptly; ABORT can be received during execution. The unused generic runner remains. |
| P10 | Progress clock | Retained: elapsed-time estimates remain separate from completion. |
| P11 | Fault/recovery | Repair: host work stops, old replay is retired and ERROR persists. Open limit: broken-link shutdown and operator recovery require target proof. |
| P12 | Startup telemetry/identity | Retained: existing bounded probe attempts and observation fields. |
| P13 | ACK/resend/barriers | Repair: JOB preserves outstanding transport slots; faulted receive batches cannot replay/resynchronize movement. Healthy transport recovery retained. |
| S01 | Pending-job handoff | Repair through real ACK validation; existing pending dispatch/conflict rules retained. |
| S02 | Upload/spool/registration | Retained: existing validation and pending registration. Power-loss/durable transaction guarantees are not introduced. |
| S03 | Observed readiness | Repair: UI/Web expire connectivity after five seconds without publication; cached observations do not imply readiness. |
| S04 | Retained filename | Repair: rejection cannot replace displayed job identity. |
| S05 | Stop suppression | Retained: existing suppression guard; backend duplicate ABORT is now idempotent during cleanup. |
| S06 | Preheat tracking | Repair inherited from ACK validation and native fault precedence; existing event classification retained. |
| S07 | Print history | Retained: terminal transitions still drive history. Expiring connectivity does not synthesize completion. |
| U01 | Navigation/lifetime | Repair in leveling and maintenance screens: timers retain operation ownership without dereferencing destroyed widgets. Other screens retained. |
| U02 | USB start | Repair inherited from ACK validation; rejected starts do not report success. Existing selection/conflict flow retained. |
| U03 | Material workflow | Repair: observed operation completion/cancellation replaces estimated duration; cancellation/error returns the local workflow to a usable state. Hands-on extrusion proof remains open. |
| U04 | Leveling | Repair: one UI-thread owner observes backend operation results and sends ABORT on Cancel/exit. Next start homes through its existing first step; Cancel does not itself promise rehoming. |
| U05 | Network import worker | Retained. Screen cancellation still concerns UI lifetime, not rollback of external network changes. Thread/reentry stress is not proven here. |
| U06 | WiFi toggle worker | Retained: busy guards and refresh of persisted state. No new stress or device-network claim. |
| U07 | Network application | Open limit: UCI commit and runtime reload remain sequential, without transactional rollback. |
| U08 | Update launch | Repair: shared maintenance lock, duplicate-launch guard and observed/unknown result states. Full reboot/rollback presentation needs target proof. |
| U09 | Factory reset | Repair: same maintenance ownership and child-exit handling. No reset was executed. |
| U10 | Local Digital Factory protocol | Repair: accepted command plus matching observed state is required; rejected/timeout results are failures. |
| U11 | Frame lights | Repair inherited from ACK validation; existing bounded startup retries and desired-state persistence retained. |
| U12 | Thumbnail cache | Retained: existing file-identity invalidation and job lifecycle cleanup. |
| C01 | Pairing/account binding | Retained: existing authorization/binding rules; local action-result correction applies through U10. Live cloud pairing not exercised. |
| C02 | Reconnect/freshness | Repair: command-aware backoff and actual reconnect after stale application status. |
| C03 | TLS/WebSocket setup | Open limit: DNS, connection and TLS/read paths remain synchronous; backoff responsiveness does not prove cancellation during those calls. |
| C04 | WebSocket assembly | Retained: existing fragmented-message parser and bounds. No new protocol conformance claim. |
| C05 | Cloud job delivery | Retained: local upload acceptance remains distinct from print completion; downstream native ACK failures now propagate. |
| W01 | Setup/access mode | Retained: no authentication redesign. |
| W02 | Session tokens | Retained: process-local expiry/eviction and restart behavior. |
| W03 | Digest nonce lifecycle | Open limit: nonce-count replay enforcement is still absent; the dormant field is not represented as implemented protection. |
| W04 | Browser credentials/navigation | Retained: existing routing and token handling; obsolete SSE callbacks cannot replace the current source. |
| W05 | Browser SSE retries | Repair: one owned timer/source; explicit disconnect cancels retries. |
| W06 | HTTP/SSE ownership | Repair: API descriptors are reclaimed on overflow, hangup and short/blocked SSE writes. Open limit: synchronous uploads and full lighttpd/proxy concurrency remain unproven. |
| W07 | ZMQ request/reply | Repair: bounded complete ACK parsing, rejection propagation and bounded/NUL-terminated status input. Existing socket recovery retained. |
| I01 | Package installation | Repair: only successful installation schedules reboot. Open limit: failure after partial mutation has no new complete rollback transaction. |
| I02 | Bootstrap transaction | Retained: existing preflight and patch/rollback helpers; their host fixtures remain applicable. |
| I03 | Service handoff | Repair: dedicated API owner plus process-level socket lock. Existing native/stock print-service handoff retained. |
| I04 | UI watchdog/fallback | Open limit: existing fallback policy retained; availability of stock assets after pruning must be proven on the intended package. |
| I05 | Firmware verification cache | Retained: existing cache/verification lifecycle. No firmware was programmed. |

Supporting mechanisms were also accounted for: G-code expansion is reused by
macro admission; serial framing is exercised by the fault/response fixture;
manual cancellation restores coordinate/extruder modes in its queued policy.
Multipart scanning, active/idle publication cadence, diagnostic suppression,
mDNS cadence, touch-marker timers and small local controls are retained.
These nine supporting groups are not nine additional repaired defects.

## Host regression coverage

| Production boundary | Regression evidence |
| --- | --- |
| Native ACK acceptance | [test_backend_rpc.c](../../web/tests/test_backend_rpc.c) drives both real UI and Web ZMQ adapters against an in-process REP peer: rejection, acceptance, truncation, empty/garbage replies and operation ID. Shared parser tests include duplicate status and embedded misleading text. |
| Native ownership/completion/faults | [test_printsvc.c](../../printsvc/tests/test_printsvc.c) covers queue admission, final ACK, cancellation, timeout, stale/superseded observations, fault-plus-resend in one serial receive batch and one-shot shutdown. |
| Pause prerequisites | The native fixture verifies that a resumed job remains at its heat gate while the bed is cold, advances when ready, and retains an in-file heater wait. |
| API connection ownership | [test_sse.c](../../web/tests/test_sse.c) covers over-capacity descriptors, disconnected/slow readers and a second startup preserving the live Unix socket. |
| Browser retry ownership | [test_api_sse.cjs](../../web/tests/test_api_sse.cjs) executes the production script with controlled EventSource/timers, including duplicate errors and obsolete callbacks. |
| Cloud retry and action outcomes | [test_reconnect.c](../../dfsvc/tests/test_reconnect.c) wakes a 120-second backoff with a queued local command. [test_df_bridge.c](../../web/tests/test_df_bridge.c) checks the action-completion predicate; it is not a live broker/cloud end-to-end test. |
| Maintenance ownership | The native fixture exercises the actual fork/flock/waitpid helper with harmless children, concurrent launch rejection and nonzero exit. |
| Installer failure | [deneb-state-transitions-selftest.sh](../../tools/deneb-state-transitions-selftest.sh) executes installer main control flow with every device action mocked: validation failure cannot schedule reboot; success can. |
| Init ownership policy | Native source audit and its negative fixtures require the dedicated API service and reject a second Web-owned API instance. |

Native UI builds cover both the stub and real ZMQ adapter. LVGL touchscreen
interaction is source-reviewed, not exercised on a display. Native and Web
fixtures use synthetic controller/cloud inputs; they do not establish physical
motion, cooling, real service restarts or live pairing.

The fault-priority, SSE overflow and duplicate browser-retry assertions were
observed failing before the corresponding source repairs and passing afterward.
Other cases use production-path fixtures and causal source traces; they are
not all claimed as individually reproduced on the baseline.

## Acceptance still required

The source repairs do not close the current physical safety/release blockers.
A current package needs supervised Pause/Resume/Stop, material and leveling
checks, reboot/recovery proof and multi-client proxy/upload testing under the
repository safety contract. M401 is queued, not an emergency interrupt, and
the one-shot fault shutdown can fail on a broken link or sequence gap.
Do not infer cooling from a submitted command or mechanical stability from
software completion alone.
