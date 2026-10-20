/**
 * @file    SpiDrv_Baud.h
 * @brief   Pure computation of LPSPI baud / timing fields (no hardware access, host testable).
 *          SCK = f_func / (2^PRESCALE * (SCKDIV + 2)),   [RM 51.3.1.12, 51.3.1.15]
 */
#ifndef SPIDRV_BAUD_H
#define SPIDRV_BAUD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "SpiDrv_Types.h"

typedef struct
{
    uint8   Prescale;   /**< TCR[PRESCALE] 0..7 (divide by 2^Prescale). */
    uint8   SckDiv;     /**< CCR[SCKDIV]  0..255 (period = SckDiv + 2 cycles). */
    uint8   PcsSck;     /**< CCR[PCSSCK]  delay = PcsSck + 1 cycles. */
    uint8   SckPcs;     /**< CCR[SCKPCS]  delay = SckPcs + 1 cycles. */
    uint8   Dbt;        /**< CCR[DBT]     delay = Dbt + 2 cycles. */
    uint32  ActualHz;   /**< Resulting SCK (master). Always <= BaudHz. */
} SpiDrv_BaudResultType;

/**
 * Master: smallest prescaler that gives a divider <= 257, so resolution is maximal.
 * Slave : Prescale = 0, only checks f_func >= 2 * BaudHz (RM Table 51-4); delays are unused.
 * Returns SPIDRV_BAD_PARAM when the baud is too low or a delay does not fit the 8-bit fields,
 * SPIDRV_CLOCK_ERROR when the slave clock is too slow.
 */
SpiDrv_StatusType SpiDrv_Baud_Calc(uint32 FuncHz, uint32 BaudHz,
                                   uint32 Cs2ClkNs, uint32 Clk2CsNs, uint32 Cs2CsNs,
                                   boolean IsMaster, SpiDrv_BaudResultType *Out);

#ifdef __cplusplus
}
#endif
#endif /* SPIDRV_BAUD_H */
