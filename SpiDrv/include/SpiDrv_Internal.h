/**
 * @file    SpiDrv_Internal.h
 * @brief   Private definitions shared by the SpiDrv source files. Not for the application.
 */
#ifndef SPIDRV_INTERNAL_H
#define SPIDRV_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "SpiDrv_Types.h"

#define SPIDRV_INVALID_UNIT         ((uint8)0xFFU)

/* API ids for Det reports */
#define SPIDRV_API_INIT             ((uint8)0x01U)
#define SPIDRV_API_DEINIT           ((uint8)0x02U)
#define SPIDRV_API_SYNC             ((uint8)0x03U)
#define SPIDRV_API_ASYNC            ((uint8)0x04U)
#define SPIDRV_API_CANCEL           ((uint8)0x05U)
#define SPIDRV_API_SETBAUD          ((uint8)0x06U)
#define SPIDRV_API_SETPARAM         ((uint8)0x07U)

#if (SPIDRV_USE_DET == STD_ON)
#include "Det.h"
#define SPIDRV_DET(api, err)        ((void)Det_ReportError(SPIDRV_MODULE_ID, 0U, (api), (err)))
#else
#define SPIDRV_DET(api, err)        do { } while (0)
#endif

/** Called by the backend when an asynchronous transfer ends (ISR or polling context). */
void SpiDrv_OnTransferEnd(uint8 Instance, SpiDrv_StatusType Result);

#ifdef __cplusplus
}
#endif
#endif /* SPIDRV_INTERNAL_H */
