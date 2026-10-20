/**
 * @file    SpiDrv_Baud.c
 * @brief   Baud rate and timing calculation for LPSPI. See SpiDrv_Baud.h.
 */
#include "SpiDrv_Baud.h"

#define BAUD_PRESCALE_MAX       (7U)
#define BAUD_SCK_CYCLES_MIN     (2U)
#define BAUD_SCK_CYCLES_MAX     (257U)
#define BAUD_PCSSCK_CYC_MAX     (256U)   /* delay = field + 1, field <= 255 */
#define BAUD_SCKPCS_CYC_MAX     (256U)
#define BAUD_DBT_CYC_MIN        (2U)     /* delay = field + 2 */
#define BAUD_DBT_CYC_MAX        (257U)

/* Nanoseconds -> cycles of a clock running at TickHz, rounded up (never shorter than asked). */
static uint32 SpiDrv_Baud_NsToCycles(uint32 Ns, uint32 TickHz)
{
    uint64 Cycles = (((uint64)Ns * (uint64)TickHz) + 999999999ULL) / 1000000000ULL;
    return (Cycles > 0xFFFFUL) ? 0xFFFFUL : (uint32)Cycles;
}

SpiDrv_StatusType SpiDrv_Baud_Calc(uint32 FuncHz, uint32 BaudHz,
                                   uint32 Cs2ClkNs, uint32 Clk2CsNs, uint32 Cs2CsNs,
                                   boolean IsMaster, SpiDrv_BaudResultType *Out)
{
    SpiDrv_StatusType St = SPIDRV_OK;
    uint32 Prescale;
    uint32 TickHz = 0U;
    uint32 Div = 0U;
    boolean Found = FALSE;
    uint32 Cyc;

    if ((NULL_PTR == Out) || (0U == FuncHz) || (0U == BaudHz))
    {
        return SPIDRV_BAD_PARAM;
    }
    Out->Prescale = 0U;
    Out->SckDiv   = 0U;
    Out->PcsSck   = 0U;
    Out->SckPcs   = 0U;
    Out->Dbt      = 0U;
    Out->ActualHz = 0U;

    if (FALSE == IsMaster)
    {
        /* Slave: functional clock after prescale must be >= 2 x SCK of the external master. */
        if ((FuncHz / 2U) < BaudHz)
        {
            St = SPIDRV_CLOCK_ERROR;
        }
        else
        {
            Out->ActualHz = BaudHz;
        }
        return St;
    }

    /* Master: smallest prescaler for which SCKDIV + 2 fits into 8 bits. */
    for (Prescale = 0U; (Prescale <= BAUD_PRESCALE_MAX) && (FALSE == Found); Prescale++)
    {
        uint64 D;
        TickHz = FuncHz >> Prescale;
        D = (((uint64)TickHz + (uint64)BaudHz) - 1ULL) / (uint64)BaudHz;   /* ceil -> actual <= requested */
        if (D < (uint64)BAUD_SCK_CYCLES_MIN)
        {
            D = (uint64)BAUD_SCK_CYCLES_MIN;                                /* requested above the maximum */
        }
        if (D <= (uint64)BAUD_SCK_CYCLES_MAX)
        {
            Div = (uint32)D;
            Found = TRUE;
            Prescale--;     /* compensate the loop increment */
        }
    }
    if (FALSE == Found)
    {
        return SPIDRV_BAD_PARAM;    /* baud too low even with prescaler 128 */
    }

    TickHz = FuncHz >> Prescale;
    Out->Prescale = (uint8)Prescale;
    Out->SckDiv   = (uint8)(Div - 2U);
    Out->ActualHz = TickHz / Div;

    /* PCS -> SCK delay: (PCSSCK + 1) cycles, minimum 1 */
    Cyc = SpiDrv_Baud_NsToCycles(Cs2ClkNs, TickHz);
    if (Cyc < 1U) { Cyc = 1U; }
    if (Cyc > BAUD_PCSSCK_CYC_MAX) { return SPIDRV_BAD_PARAM; }
    Out->PcsSck = (uint8)(Cyc - 1U);

    /* SCK -> PCS delay: (SCKPCS + 1) cycles, minimum 1 */
    Cyc = SpiDrv_Baud_NsToCycles(Clk2CsNs, TickHz);
    if (Cyc < 1U) { Cyc = 1U; }
    if (Cyc > BAUD_SCKPCS_CYC_MAX) { return SPIDRV_BAD_PARAM; }
    Out->SckPcs = (uint8)(Cyc - 1U);

    /* PCS negate -> next PCS assert: (DBT + 2) cycles, minimum 2 */
    Cyc = SpiDrv_Baud_NsToCycles(Cs2CsNs, TickHz);
    if (Cyc < BAUD_DBT_CYC_MIN) { Cyc = BAUD_DBT_CYC_MIN; }
    if (Cyc > BAUD_DBT_CYC_MAX) { return SPIDRV_BAD_PARAM; }
    Out->Dbt = (uint8)(Cyc - BAUD_DBT_CYC_MIN);

    return SPIDRV_OK;
}
