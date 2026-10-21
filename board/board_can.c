#include "board_can.h"
#include "S32K144_PCC.h"
#include "S32K144_PORT.h"
#include <stddef.h>

#define BOARD_CAN_PIN_FIELDS (PORT_PCR_MUX_MASK | PORT_PCR_IRQC_MASK | \
                              PORT_PCR_PE_MASK | PORT_PCR_PS_MASK)
#define BOARD_CAN_PIN_VALUE PORT_PCR_MUX(5U)

static BoardCan_Diagnostics s_board = { .status = BOARD_CAN_NOT_RUN };

BoardCan_Status BoardCan_InitPins(void)
{
    const BoardCan_Diagnostics empty = { .status = BOARD_CAN_NOT_RUN };
    s_board = empty;
    s_board.pcc_porte = IP_PCC->PCCn[PCC_PORTE_INDEX];
    if ((s_board.pcc_porte & (PCC_PCCn_CGC_MASK | PCC_PCCn_PR_MASK)) !=
        (PCC_PCCn_CGC_MASK | PCC_PCCn_PR_MASK))
    {
        s_board.status = BOARD_CAN_CLOCK_DISABLED;
        return s_board.status;
    }
    s_board.pcr4_before = IP_PORTE->PCR[4U];
    s_board.pcr5_before = IP_PORTE->PCR[5U];
    s_board.pcr4_after = s_board.pcr4_before;
    s_board.pcr5_after = s_board.pcr5_before;
    s_board.pcr_valid = 1U;
    if ((((s_board.pcr4_before & PORT_PCR_LK_MASK) != 0U) &&
         ((s_board.pcr4_before & BOARD_CAN_PIN_FIELDS) != BOARD_CAN_PIN_VALUE)) ||
        (((s_board.pcr5_before & PORT_PCR_LK_MASK) != 0U) &&
         ((s_board.pcr5_before & BOARD_CAN_PIN_FIELDS) != BOARD_CAN_PIN_VALUE)))
    {
        s_board.status = BOARD_CAN_PIN_LOCKED;
        return s_board.status;
    }
    /* ISF is W1C: write zero to preserve evidence; never replay its read 1.
     * A compatible locked pin needs no write. Preserve all other pad fields.
     */
    if ((s_board.pcr4_before & PORT_PCR_LK_MASK) == 0U)
    {
        IP_PORTE->PCR[4U] = (s_board.pcr4_before &
            ~(BOARD_CAN_PIN_FIELDS | PORT_PCR_ISF_MASK)) | BOARD_CAN_PIN_VALUE;
    }
    if ((s_board.pcr5_before & PORT_PCR_LK_MASK) == 0U)
    {
        IP_PORTE->PCR[5U] = (s_board.pcr5_before &
            ~(BOARD_CAN_PIN_FIELDS | PORT_PCR_ISF_MASK)) | BOARD_CAN_PIN_VALUE;
    }
    s_board.pcr4_after = IP_PORTE->PCR[4U];
    s_board.pcr5_after = IP_PORTE->PCR[5U];
    s_board.status = (((s_board.pcr4_after & BOARD_CAN_PIN_FIELDS) == BOARD_CAN_PIN_VALUE) &&
                      ((s_board.pcr5_after & BOARD_CAN_PIN_FIELDS) == BOARD_CAN_PIN_VALUE)) ?
                     BOARD_CAN_OK : BOARD_CAN_VERIFY_FAILED;
    return s_board.status;
}

BoardCan_Status BoardCan_GetDiagnostics(BoardCan_Diagnostics *out)
{
    if (out == NULL)
    {
        return BOARD_CAN_INVALID_ARGUMENT;
    }
    *out = s_board;
    return BOARD_CAN_OK;
}
