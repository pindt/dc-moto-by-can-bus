# Simplified Phase 4 CAN Application

## Polling architecture

`CanApp_Service(now_ms, time_valid)` performs one bounded round:

1. Observe shared faults and check the KEEPALIVE communication deadline.
2. Poll CAN diagnostics, including the driver's latched bus-off indication.
3. Poll one RX frame; use the unchanged codec to validate ID, DLC and bytes.
4. Publish commands or answer protocol-only requests; latch STOP immediately.
5. Move one terminal shared result into an available retained reply slot.
6. Poll existing TX completion, then submit at most one next frame.

There are no software TX frame queues, history table, allocations, retry waits,
background telemetry or SPI/motor calls. `src/main.c` remains the Phase 3 path;
`examples/can_only/main.c` is the separate Phase 4 path.

## Shared data

`app_shared` holds one ordinary command, one separate priority STOP, one
motor-status/result snapshot and a persistent inhibit/first-fault latch.
Each request carries the original command, a nonzero increasing request ID and
IDLE/PENDING/DONE/ERROR state. A private taken flag allows exactly one consumer
to take a pending request without adding a PROCESSING lifecycle state.

CAN publishes; the future SPI Application takes, checks ownership and publishes
real status/completion. There is one terminal result slot. It cannot be
replaced until CAN copies it into its reply slot and acknowledges the ID.
Publishing another result returns false and must be retried on a later round.

STOP/fault marks pending ordinary work ERROR/SAFETY_INTERLOCK immediately.
The old ID ceases to be current before any CAN reply. Cancellation is a software
revocation, not proof of physical safe-off. STOP remains independently consumable
even while the cancelled result waits for CAN. Success requires backend evidence
and the matching taken request ID. IDs never wrap/reuse within a boot.

## Minimum response storage

- One ordinary semantic reply slot: ACK/NACK plus optional GET_STATUS STATUS.
- One priority STOP semantic reply slot: ACK/NACK.
- One pending ERROR flag, coalescing the first sticky fault notification.
- One stable in-flight CAN frame, untouched until PollTx consumes completion.

Selection is STOP reply, ERROR, ordinary ACK, ordinary STATUS. An accepted MB0
transfer is never overwritten or preempted. BUSY returns immediately; RX and
safety continue on every service round. After an uncertain TX API failure, new
TX is frozen until coordinated owner recovery; no blind retry or abort occurs.

Normal overload is explicit backpressure: if the appropriate reply slot is
occupied, the incoming request is counted in rx_dropped and not accepted or
sequence-advanced. STOP still inhibits. Results remain shared until a reply slot
can retain them. Arbitrary traffic is not lossless with one hardware RX mailbox.
The GUI should retry identical bytes after a missing response, using a bounded
GUI-side deadline, and receive both responses for GET_STATUS.

## Protocol and duplicate policy

The original codec/IDs/opcodes/result values are unchanged: standard Classic CAN,
DLC 8, command 0x100, response 0x180. Wrapper metadata checks precede the codec.
Codec-invalid data is counted and NACKed when correlation bytes are available;
wrong ID or insufficient bytes do not create a command.

One latest-request record and the active shared requests protect duplicates.
Same sequence plus identical bytes replays its latest result, without actuation.
Changed bytes on a protected sequence return SEQUENCE_CONFLICT. New sequences
advance 1..127 modulo 256; half-range distance 128..255 is stale. Uncached older
sequences return STALE_SEQUENCE. Rejected but processed new requests consume
sequence; dropped requests and malformed payloads do not. Use a new sequence
for a new attempt after BUSY/NOT_CONFIGURED changes. No historical replay is
promised after moving on to newer GUI traffic.

GET_STATUS returns OK then backend evidence. Without a backend it reports
UNCONFIGURED, no CONFIGURED/SAFE_OFF claims and no invented motor duty.
KEEPALIVE acknowledges receipt; only reliable time and a new request refresh the
lease. SET_MAX_DUTY returns OK for CAN software admission policy only, constrains
subsequent SET_DUTY and never claims physical application. START/duty require
backend readiness and a fresh timed lease. CLEAR_FAULT is backend work and does
not bypass inhibit or recover sticky application faults. STOP returns PENDING
when its request is published, including without a backend; only a matching
backend result can complete it. Even stale/conflicting fully validated STOP
latches inhibit, though no new request ID is allocated for that rejected request.

## Timing and fault behavior

`CanApp_Config.communication_timeout_ms` must be nonzero and below 2^31 for
physical START/duty. Supply real monotonic uint32 milliseconds. Elapsed unsigned
subtraction supports normal wrap; service must run regularly. Expiry is checked
before RX, so a late KEEPALIVE cannot revive permission. Time loss after a lease
was established also latches inhibit. Duplicate KEEPALIVE does not refresh it.
The simplified module removes operation/TX deadlines; pending work never turns
into success by elapsed time. The real backend must bound its own operations.

The actual OsIf configuration has SYSTEM_TIMER and CUSTOM_TIMER OFF, with dummy
low-level timeout counters. The CAN-only main uses `{0U}` and Service(0,false),
so physical START/duty stays disabled. No loop count is treated as milliseconds.

RX overrun latches inhibit before processing the returned valid frame. Invalid
wrapper metadata, API failures, driver FAULT and bus-off also inhibit. No new TX
is submitted while bus-off is reported, though an existing owned TX can be
polled while the wrapper remains RUNNING. An ERROR cannot be guaranteed on a
broken bus. Faults are sticky; no automatic driver or motor recovery is added.

For STOP-only inhibit, a foreground supervisor may explicitly release it after
both request slots are empty and real ready, fault-free STOPPED/safe-off evidence
exists. Fault inhibits require coordinated physical safe shutdown and owner
reinitialization. Never reinitialize to bypass outstanding motor work or TX.

All state has one foreground owner. Volatile diagnostics are for debugger
observation, not synchronization. Interrupt/DMA/RTOS access needs a separately
reviewed serialization design. No physical motor behavior has been verified.
