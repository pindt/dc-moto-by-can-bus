# Phase 4 simplification

## Inspected before editing

The previous `can_app.c` had a 16-entry history, eight ordinary and four urgent
TX frame slots, cache eviction, per-request and TX deadline tracking, and fourteen
event counters. Shared data duplicated command/result lifecycle in two mailboxes,
used six lifecycle states, and maintained inhibit generations and a fault mask.
There was no periodic telemetry scheduler: STATUS was already request-driven.
The existing codec/wrapper already supplied frame validation, polling and real
TX ownership; those functions did not need changes.

The proposed architecture and source-change plan were presented in chat before
editing. Changes are limited to application, its separate main, tests and docs.

## Result

```
CAN wrapper -> unchanged codec -> CAN Application -> ordinary command mailbox
                                        |        -> priority STOP/inhibit
                                        ^
                              one motor-status/result snapshot
                                        ^
                                future SPI Application
```

| Feature | Action |
| --- | --- |
| CAN/SPI drivers, RTD, protocol bytes/IDs/codes | Retained unchanged |
| Working Phase 3 src/main.c and regression fixture | Retained unchanged |
| Ordinary/priority frame queues | Removed; one ordinary reply slot, one STOP reply slot, one in-flight frame, one ERROR-due flag |
| Six lifecycle states | Replaced by IDLE, PENDING, DONE, ERROR; one taken flag distinguishes unread vs owned pending work |
| Two independent result records | Replaced by one retained terminal result in the motor-status/result snapshot |
| 16-entry history and eviction | Removed; replay latest GUI request and active shared requests only |
| Inhibit generations and multi-fault mask | Removed; foreground latch and first fault are retained |
| Per-history-operation and TX timers | Removed; one reliable-time KEEPALIVE communication watchdog retained |
| Fourteen event counters | Reduced to malformed, rx_overruns, rx_dropped plus current fault/API/TX/lease fields |
| Periodic telemetry | None added; STATUS only answers GET_STATUS |
| SET_MAX_DUTY | Retained as software admission policy, not applied hardware configuration |
| Real-time dependency | Retained explicitly; no valid clock means physical START/duty disabled |
| SPI implementation / target mock | None added; synthetic evidence exists only in host tests |

## Deliberate behavior boundaries

- PENDING still means accepted. COMPLETED still requires matching backend DONE
  with actual evidence. ERROR represents rejection/failure/cancellation using
  existing protocol NACK codes; no new wire values were introduced.
- The GUI must use increasing sequences, send one request at a time, and retry
  identical bytes if no reply arrives. For GET_STATUS, receive ACK and STATUS.
  New ordinary requests arriving while its reply slot is occupied are not
  accepted or sequence-advanced; `rx_dropped` records this bounded backpressure.
- A valid STOP latches inhibit before sequence or reply-capacity checks. A busy
  STOP reply can delay acknowledgment, but never the inhibit or revocation of
  pending START/duty. Already accepted CAN TX cannot be preempted.
- The one terminal shared result can delay a second result publication. The
  backend must perform safe-off immediately and retry publication on a later
  foreground round, never wait for result storage or CAN transmission.
- Latest/active duplicates replay without republishing hardware work. Older
  requests now get STALE_SEQUENCE rather than historical replay. This changes
  replay retention, not the byte layout or the meaning of any result code.
- A fresh KEEPALIVE needs a new sequence. Replayed KEEPALIVEs do not extend the
  watchdog. Deadlines, invalid time and CAN/backend faults revoke pending work.
- CLEAR_FAULT now follows the same inhibit admission check as other ordinary
  requests; the previous shared-layer special case allowed it during inhibit.
  This explicit application policy prevents it bypassing a priority STOP. After
  confirmed STOP-only recovery, it remains a backend command. Opcode bytes and
  result-code meanings are unchanged.
- Fault recovery remains explicit. No automatic bus-off reset or physical motor
  recovery is implemented. A real backend must enforce its own operation bounds
  and hardware watchdog; only the communication watchdog belongs here.

## Source changes

- Reworked `application/can_app.c/.h` and `application/app_shared.c/.h`.
- Simplified `examples/can_only/main.c` initialization and updated its one-field
  timeout configuration. Existing pin entries are copied, not edited.
- Replaced history/queue-specific host cases with simplified ownership/safety
  tests. Kept the strict host runner and wrapper lifetime-checking double.
- Updated design, report and SPI contract. Added exact build/test instructions.
- Kept the standalone ARM build script and ESP32-S3 bench wire vectors.

The C interface intentionally changes: use APP_IDLE/PENDING/DONE/ERROR,
`AppShared_ReadSnapshot()` for motor/result data, `AppShared_ReadResult()` without
an ordinary/priority selector, and `AppShared_ReleaseInhibit()` without a
generation. Adapt the future teammate's code to the updated contract. The wire
codec is unchanged.

See `Phase4_CAN_Build_and_Test.md` and `SPI_Application_Interface_Contract.md`.

