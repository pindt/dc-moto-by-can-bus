/**
 * @file    SpiDrv.h
 * @brief   Public API of SpiDrv: middle layer between the application and the RTD IP layer
 *          (Lpspi_Ip, Clock_Ip, IntCtrl_Ip). Prerequisites, done once by the system init:
 *          Clock_Ip_Init, Port_Ci_Port_Ip_Init, Dma_Ip_Init (if DMA used), IntCtrl_Ip_Init, OsIf_Init.
 */
#ifndef SPIDRV_H
#define SPIDRV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "SpiDrv_Types.h"

/* ---------------------------------- lifecycle ---------------------------------- */
SpiDrv_StatusType SpiDrv_Init(const SpiDrv_ConfigType *Cfg);
SpiDrv_StatusType SpiDrv_DeInit(void);

/* ---------------------------------- transfers ---------------------------------- */
/**
 * Blocking full-duplex transfer. Length is in BYTES. Tx or Rx may be NULL (not both):
 * NULL Tx sends the device DefaultData, NULL Rx discards received data.
 * TimeoutUs is the time allowed for one frame (RTD semantic, needs a real OsIf counter).
 */
SpiDrv_StatusType SpiDrv_SyncTransfer(SpiDrv_DeviceId Dev, const uint8 *Tx, uint8 *Rx,
                                      uint16 Length, uint32 TimeoutUs);

/**
 * Non-blocking transfer. Buffers must stay valid until the callback / status shows completion.
 * With DMA configured in the Peripherals tool, buffers must be in DMA-reachable memory.
 */
SpiDrv_StatusType SpiDrv_AsyncTransfer(SpiDrv_DeviceId Dev, const uint8 *Tx, uint8 *Rx,
                                       uint16 Length, SpiDrv_CallbackType Callback, void *UserData);

SpiDrv_StatusType SpiDrv_Cancel(SpiDrv_DeviceId Dev);

/** Polling mode only: call periodically (e.g. from a 1 ms task) to progress async transfers. */
void SpiDrv_MainFunction(void);

/* ---------------------------------- status ---------------------------------- */
SpiDrv_UnitStatusType SpiDrv_GetUnitStatus(uint8 UnitIdx);
SpiDrv_StatusType     SpiDrv_GetLastError(uint8 UnitIdx);
SpiDrv_StatusType     SpiDrv_ClearFault(uint8 UnitIdx);

/* ---------------------------------- runtime changes (unit must not be BUSY) -------- */
/** Only for devices configured with SPIDRV_OVR_BUS. ActualHz may be NULL. */
SpiDrv_StatusType SpiDrv_SetBaud(SpiDrv_DeviceId Dev, uint32 BaudHz, uint32 *ActualHz);
SpiDrv_StatusType SpiDrv_SetFrameSize(SpiDrv_DeviceId Dev, uint8 FrameBits);
SpiDrv_StatusType SpiDrv_SetLsbFirst(SpiDrv_DeviceId Dev, boolean LsbFirst);
SpiDrv_StatusType SpiDrv_SetDefaultData(SpiDrv_DeviceId Dev, uint32 DefaultData);
SpiDrv_StatusType SpiDrv_SetMode(uint8 UnitIdx, SpiDrv_ModeType Mode);

#ifdef __cplusplus
}
#endif
#endif /* SPIDRV_H */
