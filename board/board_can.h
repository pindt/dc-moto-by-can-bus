#ifndef BOARD_CAN_H
#define BOARD_CAN_H

#include <stdint.h>

typedef enum
{
    BOARD_CAN_OK = 0,
    BOARD_CAN_CLOCK_DISABLED,
    BOARD_CAN_PIN_LOCKED,
    BOARD_CAN_VERIFY_FAILED,
    BOARD_CAN_NOT_RUN,
    BOARD_CAN_INVALID_ARGUMENT
} BoardCan_Status;

typedef struct
{
    BoardCan_Status status;
    uint32_t pcc_porte;
    uint32_t pcr4_before;
    uint32_t pcr5_before;
    uint32_t pcr4_after;
    uint32_t pcr5_after;
    uint8_t pcr_valid; /* No PCR access is attempted with the clock disabled. */
} BoardCan_Diagnostics;

/* Startup, single caller. NORMAL only, after clocks and verified electrical
 * compatibility/SBC RX isolation. Does not enable clocks or initialize CAN.
 * Rejects an incompatible locked pin before changing either pin.
 */
BoardCan_Status BoardCan_InitPins(void);
/* Cache-only, no peripheral access. */
BoardCan_Status BoardCan_GetDiagnostics(BoardCan_Diagnostics *out);

#endif
