/**
 * @file    SpiDrv_Platform.c
 * @brief   Adapter to Clock_Ip, IntCtrl_Ip and SchM_Spi of the RTD.
 */
#include "SpiDrv_Platform.h"
#include "SpiDrv_Cfg.h"
#include "Clock_Ip.h"
#include "IntCtrl_Ip.h"
#include "Lpspi_Ip_Cfg.h"
#include "SchM_Spi.h"

/* ISR entry points implemented in RTD/src/Lpspi_Ip_Irq.c */
#if defined(LPSPI_IP_0_ENABLED) && (LPSPI_IP_0_ENABLED == STD_ON)
extern void Lpspi_Ip_LPSPI_0_IRQHandler(void);
#endif
#if defined(LPSPI_IP_1_ENABLED) && (LPSPI_IP_1_ENABLED == STD_ON)
extern void Lpspi_Ip_LPSPI_1_IRQHandler(void);
#endif
#if defined(LPSPI_IP_2_ENABLED) && (LPSPI_IP_2_ENABLED == STD_ON)
extern void Lpspi_Ip_LPSPI_2_IRQHandler(void);
#endif

static boolean SpiDrv_Plat_GetIrq(uint8 Instance, IRQn_Type *Irq, IntCtrl_Ip_IrqHandlerType *Handler)
{
    boolean Found = TRUE;
    switch (Instance)
    {
#if defined(LPSPI_IP_0_ENABLED) && (LPSPI_IP_0_ENABLED == STD_ON)
        case 0U: *Irq = LPSPI0_IRQn; *Handler = &Lpspi_Ip_LPSPI_0_IRQHandler; break;
#endif
#if defined(LPSPI_IP_1_ENABLED) && (LPSPI_IP_1_ENABLED == STD_ON)
        case 1U: *Irq = LPSPI1_IRQn; *Handler = &Lpspi_Ip_LPSPI_1_IRQHandler; break;
#endif
#if defined(LPSPI_IP_2_ENABLED) && (LPSPI_IP_2_ENABLED == STD_ON)
        case 2U: *Irq = LPSPI2_IRQn; *Handler = &Lpspi_Ip_LPSPI_2_IRQHandler; break;
#endif
        default: Found = FALSE; break;
    }
    return Found;
}

uint32 SpiDrv_Plat_GetFuncClockHz(uint8 Instance)
{
    uint32 Hz = 0U;
#if defined(CLOCK_IP_GET_FREQUENCY_API) && (CLOCK_IP_GET_FREQUENCY_API == STD_ON)
    /* Real value from the Clock driver; 0 if the module clock is gated. */
    switch (Instance)
    {
#if defined(CLOCK_IP_HAS_LPSPI0_CLK)
        case 0U: Hz = Clock_Ip_GetClockFrequency(LPSPI0_CLK); break;
#endif
#if defined(CLOCK_IP_HAS_LPSPI1_CLK)
        case 1U: Hz = Clock_Ip_GetClockFrequency(LPSPI1_CLK); break;
#endif
#if defined(CLOCK_IP_HAS_LPSPI2_CLK)
        case 2U: Hz = Clock_Ip_GetClockFrequency(LPSPI2_CLK); break;
#endif
        default: break;
    }
#else
    /* API disabled in the Clock tool: use the fixed values from SpiDrv_Cfg.h */
    switch (Instance)
    {
        case 0U: Hz = (uint32)SPIDRV_FUNC_CLOCK_HZ_LPSPI0; break;
        case 1U: Hz = (uint32)SPIDRV_FUNC_CLOCK_HZ_LPSPI1; break;
        case 2U: Hz = (uint32)SPIDRV_FUNC_CLOCK_HZ_LPSPI2; break;
        default: break;
    }
#endif
    return Hz;
}

void SpiDrv_Plat_IrqEnable(uint8 Instance, uint8 Priority)
{
    IRQn_Type Irq;
    IntCtrl_Ip_IrqHandlerType Handler;
    IntCtrl_Ip_IrqHandlerType Old = NULL_PTR;

    if (TRUE == SpiDrv_Plat_GetIrq(Instance, &Irq, &Handler))
    {
        IntCtrl_Ip_InstallHandler(Irq, Handler, &Old);
        IntCtrl_Ip_SetPriority(Irq, Priority);
        IntCtrl_Ip_ClearPending(Irq);
        IntCtrl_Ip_EnableIrq(Irq);
    }
}

void SpiDrv_Plat_IrqDisable(uint8 Instance)
{
    IRQn_Type Irq;
    IntCtrl_Ip_IrqHandlerType Handler;

    if (TRUE == SpiDrv_Plat_GetIrq(Instance, &Irq, &Handler))
    {
        IntCtrl_Ip_DisableIrq(Irq);
        IntCtrl_Ip_ClearPending(Irq);
    }
}

void SpiDrv_Plat_EnterCritical(void)
{
    SPIDRV_SCHM_AREA_ENTER();
}

void SpiDrv_Plat_ExitCritical(void)
{
    SPIDRV_SCHM_AREA_EXIT();
}
