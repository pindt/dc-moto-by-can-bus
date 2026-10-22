# Phase 4 CAN Application report (simplified)

Date: 2026-10-21, Asia/Bangkok.
Project: `C:\Users\Admin\workspaceS32DS.3.4\dc-moto-by-can-bus`.

## Delivered

Refactored `application/can_app.c/.h` and `app_shared.c/.h` into a single-threaded
poller: one ordinary command mailbox, separate STOP/inhibit, one status/result
snapshot, IDLE/PENDING/DONE/ERROR, two semantic reply slots and one stable
in-flight frame. Removed the history table, TX queues, cache eviction, inhibit
generations/fault masks, per-request/TX timers and most counters. A real-time
KEEPALIVE communication watchdog remains mandatory for physical START/duty.

The source-change proposal was presented before editing. Both drivers, protocol,
RTD, generated configuration, pin mappings, original main, regression fixture,
.cproject and .mex remain unchanged. No SPI Application or motor control was
implemented. The CAN-only main has a simple startup and foreground loop, including
fault servicing after initialization failure. It has no reliable clock/backend,
so physical START/duty is disabled.

## Performed verification

| Check | Observed result |
| --- | --- |
| Strict host GCC compile and execution | PASS: 14 simplified Phase 4 test groups |
| Asynchronous ownership | Matching taken IDs required, stale/cancelled IDs rejected; single result retained under backpressure |
| STOP/TX/fault ordering | PASS with in-flight buffer lifetime assertions; STOP inhibits before TX, priority STOP reply precedes ERROR and ordinary replies |
| Timeout and unavailable timer | PASS: fresh KEEPALIVE required, duplicate does not refresh, expiry/clock loss revokes work, unsigned time wrap covered |
| Protection audit | PASS: SHA256 of 138 protected existing files unchanged; Phase 3 fixture identical |
| Standalone ARM compile/link | PASS: installed S32DS GCC 6.3 / real RTD headers / existing linker and startup |
| ARM image size | text 38916, data 164, bss 6700 bytes (whole image including RTD/startup) |
| SPI isolation | PASS: no SpiDrv_/Lpspi_Ip_/original SPI transfer symbols in linked CAN-only ELF |
| Final ARM diagnostics | No warnings/errors; existing unsupported vendor section pragmas suppressed by the separate build script |

Host tests cover codec goldens and validation, normal flow, software-only max
policy, mailbox BUSY, STOP during queued/taken work, backend evidence guards,
latest/active duplicate replay, stale/conflict/wrap, result backpressure,
RX overrun, bus-off and RX/TX/diagnostic API failures, including BUSY returned together with driver FAULT. Test backend results are
synthetic host data; the target image contains no mock-selection mechanism.

Original/fixture main SHA256:
`2726994F7000C711241AE6F915BBF7AA8F2B8281A088A44C83BB7E8569080C25`.
No commits, branch changes or unrelated project writes were made.

## Intentional limits and pending verification

- Protocol bytes, IDs, opcode/result values and accepted-vs-completed meanings
  are unchanged. Replay retention is intentionally smaller: latest/active only,
  not historical. New requests while a reply slot is occupied are counted and
  not accepted; GUI retries identical bytes. See `Phase4_Simplification.md`.
- Single foreground ownership only. No DMA/interrupt/RTOS shared access support.
  No arbitrary-rate/lossless reception or unbounded response buffering.
- STOP/inhibit/cancellation/ACK are not physical safe-off confirmation. Real
  backend evidence and matching request ID are required for completion.
- Existing OsIf provides no application milliseconds. Provision real timer
  ownership and nonzero communication timeout before physical integration.
  The real SPI Application must enforce its own operation deadlines/hardware
  safety; elapsed time never invents a completion here.
- Sticky fault recovery remains coordinated system-owner work. No automatic
  bus-off restart, fault-clear bypass or physical recovery API is added.
- Hardware flashing, S32 execution, actual bus timing/overrun/bus-off, ESP32-S3
  toolchain/bench execution, and real SPI/motor integration remain **pending**.
  No physical motor control has been verified.
- The command-line S32DS-toolchain build was performed. The documented S32DS
  external-tool/debug launch workflow has not been exercised in the IDE.

Build/test commands and exact S32DS selection: `Phase4_CAN_Build_and_Test.md`.
Teammate API ownership and result retry rules: `SPI_Application_Interface_Contract.md`.

