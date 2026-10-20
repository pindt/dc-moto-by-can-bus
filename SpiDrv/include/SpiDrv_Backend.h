/**
 * @file    SpiDrv_Backend.h
 * @brief   Backend contract between SpiDrv core and the hardware IP. Implemented by
 *          SpiDrv_Lpspi.c (Lpspi_Ip). A FlexIO backend can be added by implementing the same
 *          functions in another file and selecting it at link time.
 */
#ifndef SPIDRV_BACKEND_H
#define SPIDRV_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "SpiDrv_Types.h"

SpiDrv_StatusType SpiDrv_Be_UnitInit(const SpiDrv_UnitConfigType *Unit);
SpiDrv_StatusType SpiDrv_Be_UnitDeInit(uint8 Instance);
SpiDrv_StatusType SpiDrv_Be_SetMode(uint8 Instance, SpiDrv_ModeType Mode);

SpiDrv_StatusType SpiDrv_Be_DeviceInit(uint8 DevIdx, const SpiDrv_DeviceConfigType *Dev,
                                       uint8 UnitInstance, boolean IsMaster, uint32 FuncClockHz);
SpiDrv_StatusType SpiDrv_Be_SetBaud(uint8 DevIdx, const SpiDrv_DeviceConfigType *Dev,
                                    boolean IsMaster, uint32 FuncClockHz, uint32 BaudHz,
                                    uint32 *ActualHz);
SpiDrv_StatusType SpiDrv_Be_SetFrameSize(uint8 DevIdx, uint8 FrameBits);
SpiDrv_StatusType SpiDrv_Be_SetLsb(uint8 DevIdx, boolean LsbFirst);
SpiDrv_StatusType SpiDrv_Be_SetDefaultData(uint8 DevIdx, uint32 DefaultData);

SpiDrv_StatusType SpiDrv_Be_Sync(uint8 DevIdx, const uint8 *Tx, uint8 *Rx,
                                 uint16 Length, uint32 TimeoutUs);
SpiDrv_StatusType SpiDrv_Be_Async(uint8 DevIdx, const uint8 *Tx, uint8 *Rx, uint16 Length);

void                  SpiDrv_Be_Cancel(uint8 Instance);
void                  SpiDrv_Be_Manage(uint8 Instance);
SpiDrv_UnitStatusType SpiDrv_Be_GetStatus(uint8 Instance);

#ifdef __cplusplus
}
#endif
#endif /* SPIDRV_BACKEND_H */
