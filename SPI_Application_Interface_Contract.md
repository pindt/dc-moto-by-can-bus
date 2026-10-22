# SPI Application interface (simplified Phase 4)

## Ownership

Include `application/app_shared.h`. All calls belong to one foreground loop;
no ISR, DMA callback or RTOS task may access shared state directly. Initialize
AppShared once at boot. The SPI driver and SPI Application are outside this change.

| Owner | API/use |
| --- | --- |
| CAN Application | PublishCommand / RequestStop, ReadResult, AcknowledgeResult |
| SPI Application | TakeCommand, IsCurrent, PublishStatus, PublishResult |
| Both/supervisor | ReadSnapshot, ReadCommand, ReadInhibit; latch faults immediately |
| Foreground supervisor | ReleaseInhibit for confirmed STOP-only recovery |

Storage is one ordinary command mailbox, one priority STOP request and one
App_Snapshot containing motor evidence and **one** terminal result. No heap,
retained caller pointers, result queues or historical records exist.

## Request lifecycle and result ownership

States are IDLE, PENDING, DONE, ERROR. TakeCommand returns each pending request
once, choosing STOP first. It copies the request; PENDING remains the lifecycle
while a private taken flag prevents repeated consumption. Use its 32-bit
request_id for completion, never its 8-bit wire sequence. IDs never reuse within
a boot and allocation fails safely after exhaustion.

Before each hardware-changing step, recheck IsCurrent(id) and ReadInhibit().
A copied/taken command is not continuing permission after STOP/fault. Revoked
IDs, wrong IDs, untaken requests and already completed requests reject results.

PublishStatus with actual evidence, then call:

```c
/* Real backend has obtained and published valid completion evidence. */
bool stored = AppShared_PublishResult(active_request_id, APP_DONE, CP_COMPLETED);
/* Check stored. If false, retain the outcome privately and retry on a later
 * foreground round only if this request ID is still current. Do not spin. */
```

Failure uses APP_ERROR and a defined NACK value 0x80..0x8A. OK/PENDING are not
terminal completion values. MOTOR_FAULT and INTERNAL_TIMEOUT results latch a
backend fault inhibit. DONE/COMPLETED requires:

| Command | Evidence required before PublishResult |
| --- | --- |
| START_FORWARD/REVERSE | valid ready RUNNING, no fault, matching direction |
| SET_DUTY | valid ready applied_duty_valid, matching applied duty, no fault |
| STOP | valid ready STOPPED and safe_off_confirmed |
| CLEAR_FAULT | valid ready no fault and safe_off_confirmed |

The **single** terminal result is immutable until CAN copies it into a retained
reply and calls AcknowledgeResult(id). Then its request becomes IDLE. If the
result snapshot is occupied, PublishResult returns false and leaves your pending
request current. Keep that verified outcome in your own bounded backend state
and retry once per later foreground round. Physical safe-off must never wait for
result storage or CAN TX. PublishResult success means stored, not transmitted.

## Status evidence

ReadSnapshot returns a coherent foreground copy of motor and terminal result.
Initially motor.valid/backend_ready/safe_off_confirmed and duty validity are
false, and state is UNCONFIGURED. Only the real backend publishes motor status:

- valid: state, direction and fault are meaningful backend observations.
- backend_ready: initialization/configuration and command processing available.
- state: backend-observed state, not the desired CAN state.
- requested_duty_valid/requested_duty: meaningful demand in 0..1000 units;
  never a measurement of applied duty.
- applied_duty_valid/applied_duty: actually confirmed applied duty in 0..1000.
- safe_off_confirmed: confirmed physical safe-off, only with valid STOPPED.
- fault, reverse_supported and reverse: actual fault/capability/direction.

Contradictory readiness/running/safe-off and invalid ranges are rejected. Publish
updated evidence as hardware changes. Readiness loss during an ordinary pending
request or a published backend fault latches inhibit and revokes that request.
The module cannot measure hardware or prove the freshness/truth of your evidence.

SET_MAX_DUTY stays a CAN software admission policy returning CP_OK; it is not
published as applied hardware configuration. Enforce your own hardware limits.
CAN-only START/duty/CLEAR_FAULT report NOT_CONFIGURED without real readiness.

## STOP and faults

ReadInhibit on every round, independently of TakeCommand. Fault-only or
stale/conflicting STOP inhibits may have no STOP request ID. Prioritize the
backend's physical safe-off procedure whenever latched; none is implemented here.

A valid STOP latches before sequence/TX checks, marks ordinary pending work
ERROR/SAFETY_INTERLOCK and makes its ID non-current. Its cancellation result is
retained for CAN. A busy shared STOP slot is not overwritten; a new STOP can
return BUSY while still inhibiting. Even if the STOP reply slot is occupied and
no reply is stored, the inhibit still acts immediately.

TakeCommand can return STOP while the cancelled ordinary result occupies the
single result snapshot. Perform safe-off without waiting; publish STOP success
later when real evidence exists and the result slot becomes available. A failed
or cancelled operation and a CAN ACK are never proof that physical motion stopped.
Already-issued SPI transactions require your own bounded safe completion/abort.

STOP completion does not clear inhibit. ReleaseInhibit() is explicit and only
succeeds with empty ordinary/STOP slots, no terminal result, ready fault-free
STOPPED/safe-off evidence and **no latched fault**. It permits recovery from
STOP-only inhibit in the one foreground owner; there is no generation counter.
Fault recovery requires the owner to make hardware safe, resolve outstanding
work/driver TX ownership and coordinate reinitialization. CLEAR_FAULT does not
bypass inhibit or clear sticky CAN/backend application faults.

## Foreground scheduling and timing

Call CAN service and your bounded SPI Application service every foreground round.
Check inhibit/current ID before physical steps; do not use blocking waits for
CAN completion, result storage or asynchronous SPI operations. SPI callbacks
should hand private events back to this foreground owner.

Physical START/duty needs a real monotonic millisecond clock, a nonzero
communication_timeout_ms below 2^31, and a fresh new-sequence KEEPALIVE. Timer
loss or deadline expiry revokes pending requests before processing late RX.
Duplicates do not refresh the lease. The existing OsIf configuration provides
no reliable application milliseconds; delivered CAN-only main leaves physical
START disabled and never publishes readiness.

This module owns only the communication watchdog. The real SPI Application must
bound motor operations and enforce physical watchdogs/interlocks even if the GUI
keeps sending KEEPALIVE or the CPU stalls. No SPI/motor implementation or physical
verification is included. Future concurrent access requires serialization and
new concurrency tests; volatile is not sufficient.
