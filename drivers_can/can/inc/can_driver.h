#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <can_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Singleton CAN0 adapter. Foreground, single caller, non-reentrant; no heap.
 * Caller must initialize clocks and grant exclusive CAN0 ownership first.
 * Never call alongside the original main.c CAN sequence or another CAN0 owner.
 * Lifecycle calls are synchronous RTD calls (dummy timeout counter currently).
 */
/* Generated loopback template is copied privately; only mode is overridden. */
Can_Status CanDriver_Init(const Can_Config *config);
Can_Status CanDriver_Start(void);
/* Stop refuses an unconsumed TX; call PollTx until completion first.
 * Stop discards any unread RX frame and unregisters receive storage.
 */
Can_Status CanDriver_Stop(void);
/* Requires READY or FAULT. FAULT deinit explicitly discards outstanding work. */
Can_Status CanDriver_Deinit(void);
Can_DriverState CanDriver_GetState(void);

/* Call once per foreground polling round (RUNNING only). Reads ESR1 once,
 * including its read side effects; faults also sample before being published.
 * Bus-off is latched in diagnostics; it does not cancel a pending transfer.
 */
Can_Status CanDriver_PollDiagnostics(void);
/* Cache-only, valid in every state; never reads hardware. Check out->valid.
 * Snapshots survive Stop/Deinit and failed reinitialization; successful Init
 * starts a new diagnostic session. No automatic error clearing/recovery.
 */
Can_Status CanDriver_GetDiagnostics(Can_Diagnostics *out);

/* OK means accepted, not transmitted. Input is copied before return.
 * No second request is accepted until PollTx returns OK for this request.
 */
Can_Status CanDriver_Transmit(const Can_Frame *frame);
/* Services RTD TX completion; OK consumes completion, BUSY retains ownership,
 * NO_TRANSFER means no wrapper request is outstanding. No waiting/deadline.
 */
Can_Status CanDriver_PollTx(void);
/* Services one frame and immediately registers the next RTD receive.
 * Output valid only for OK or RX_OVERRUN; NO_DATA leaves output untouched.
 * Call regularly: one hardware RX MB, no software queue or lossless guarantee.
 */
Can_Status CanDriver_PollRx(Can_Frame *frame);

#ifdef __cplusplus
}
#endif

#endif
