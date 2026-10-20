/**
 * @file    SpiDrv_PBcfg.h
 * @brief   Application configuration: LPSPI0 as MASTER on PTB2 (SCK), PTB3 (SIN/MISO),
 *          PTB4 (SOUT/MOSI), PTB5 (PCS0).
 */
#ifndef SPIDRV_PBCFG_H
#define SPIDRV_PBCFG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "SpiDrv_Types.h"

/* Index in SpiDrv_Devices[] */
#define SPIDRV_DEV_LPSPI0_MODE0     ((SpiDrv_DeviceId)0U)   /* CPOL=0 CPHA=0, 100 kHz, MSB first */
#define SPIDRV_DEV_LPSPI0_MODE3     ((SpiDrv_DeviceId)1U)   /* CPOL=1 CPHA=1,   1 MHz, MSB first */

/* Index in SpiDrv_Units[] */
#define SPIDRV_UNIT_LPSPI0          ((uint8)0U)

extern const SpiDrv_ConfigType SpiDrv_Config;

#ifdef __cplusplus
}
#endif
#endif /* SPIDRV_PBCFG_H */
