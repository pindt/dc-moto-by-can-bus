/**
 * @file    SpiDrv_Cfg.h
 * @brief   Compile-time configuration of the SpiDrv middle layer (S32K144, RTD 4.4.x IP layer).
 */
#ifndef SPIDRV_CFG_H
#define SPIDRV_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Mcal.h"

/** Number of LPSPI instances on S32K144 (LPSPI0..LPSPI2). */
#define SPIDRV_INSTANCE_COUNT       (3U)

/** Maximum number of SpiDrv units (one unit = one LPSPI instance) used by the application. */
#define SPIDRV_MAX_UNITS            (3U)

/** Maximum number of external devices (slaves/masters) described in the configuration. */
#define SPIDRV_MAX_DEVICES          (8U)

/** Enable parameter / state checks that return error codes. */
#define SPIDRV_DEV_ERROR_DETECT     (STD_ON)

/** Report development errors to Det_ReportError (needs Det.c from RTD in the project). */
#define SPIDRV_USE_DET              (STD_OFF)

/** Module id used for Det reports (vendor specific range). */
#define SPIDRV_MODULE_ID            ((uint16)255U)

/**
 * Functional clock (Hz) of each LPSPI, used ONLY when Clock_Ip_GetClockFrequency is not available,
 * i.e. CLOCK_IP_GET_FREQUENCY_API == STD_OFF in generate/include/Clock_Ip_Cfg_Defines.h.
 * Set them to the frequency selected in the Clock tool (PCC_LPSPIn[PCS] source after its divider).
 * Default 8 MHz = SIRCDIV2 (SIRC 8 MHz, divide by 1) as in the NXP example.
 * Instances that are not used may stay 0. Keep in sync with the Clock tool: SpiDrv cannot detect
 * a gated or changed clock in this mode. Alternative: enable "Get Frequency API" in the Clock tool.
 */
#define SPIDRV_FUNC_CLOCK_HZ_LPSPI0 (8000000UL)
#define SPIDRV_FUNC_CLOCK_HZ_LPSPI1 (0UL)
#define SPIDRV_FUNC_CLOCK_HZ_LPSPI2 (0UL)

/** SchM exclusive area (SchM_Enter/Exit_Spi_SPI_EXCLUSIVE_AREA_xx) used to guard SpiDrv state. */
#define SPIDRV_SCHM_AREA_ENTER()    SchM_Enter_Spi_SPI_EXCLUSIVE_AREA_00()
#define SPIDRV_SCHM_AREA_EXIT()     SchM_Exit_Spi_SPI_EXCLUSIVE_AREA_00()

#ifdef __cplusplus
}
#endif
#endif /* SPIDRV_CFG_H */
