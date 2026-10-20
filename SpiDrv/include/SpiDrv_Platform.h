/**
 * @file    SpiDrv_Platform.h
 * @brief   Adapter to the shared RTD frameworks (Clock_Ip, IntCtrl_Ip, SchM). The only place
 *          where SpiDrv touches them. Global init of those frameworks stays in the system init.
 */
#ifndef SPIDRV_PLATFORM_H
#define SPIDRV_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "SpiDrv_Types.h"

/** Functional clock of the LPSPI instance in Hz, 0 if the clock is off / instance unknown. */
uint32 SpiDrv_Plat_GetFuncClockHz(uint8 Instance);

/** Install Lpspi_Ip ISR, set priority, clear pending and enable the NVIC line. */
void SpiDrv_Plat_IrqEnable(uint8 Instance, uint8 Priority);
void SpiDrv_Plat_IrqDisable(uint8 Instance);

void SpiDrv_Plat_EnterCritical(void);
void SpiDrv_Plat_ExitCritical(void);

#ifdef __cplusplus
}
#endif
#endif /* SPIDRV_PLATFORM_H */
