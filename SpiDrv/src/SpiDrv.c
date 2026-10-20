/**
 * @file    SpiDrv.c
 * @brief   SpiDrv core: configuration check, unit/device management, state machine, error mapping.
 *          Hardware is reached only through SpiDrv_Backend.h and RTD frameworks only through
 *          SpiDrv_Platform.h.
 */
#include "SpiDrv.h"
#include "SpiDrv_Internal.h"
#include "SpiDrv_Backend.h"
#include "SpiDrv_Platform.h"

typedef struct
{
    SpiDrv_UnitStatusType   Status;
    SpiDrv_ModeType         Mode;
    uint32                  FuncClockHz;
    SpiDrv_StatusType       LastError;
    SpiDrv_CallbackType     Callback;
    void                   *UserData;
    SpiDrv_DeviceId         ActiveDev;
} SpiDrv_UnitStateType;

static const SpiDrv_ConfigType *SpiDrv_pCfg = NULL_PTR;
static SpiDrv_UnitStateType     SpiDrv_Unit[SPIDRV_MAX_UNITS];
static uint8                    SpiDrv_InstToUnit[SPIDRV_INSTANCE_COUNT];

/* ------------------------------------------------------------------------------------------ */
/*                                       local helpers                                         */
/* ------------------------------------------------------------------------------------------ */
static SpiDrv_StatusType SpiDrv_ValidateConfig(const SpiDrv_ConfigType *Cfg, uint8 *Map)
{
    uint8 i;

    if ((NULL_PTR == Cfg) || (NULL_PTR == Cfg->Units) || (NULL_PTR == Cfg->Devices) ||
        (0U == Cfg->UnitCount) || (Cfg->UnitCount > SPIDRV_MAX_UNITS) ||
        (0U == Cfg->DeviceCount) || (Cfg->DeviceCount > SPIDRV_MAX_DEVICES))
    {
        return SPIDRV_BAD_PARAM;
    }
    for (i = 0U; i < SPIDRV_INSTANCE_COUNT; i++) { Map[i] = SPIDRV_INVALID_UNIT; }

    for (i = 0U; i < Cfg->UnitCount; i++)
    {
        const SpiDrv_UnitConfigType *U = &Cfg->Units[i];
        if ((U->Instance >= SPIDRV_INSTANCE_COUNT) || (SPIDRV_INVALID_UNIT != Map[U->Instance]) ||
            (NULL_PTR == U->IpConfig) || ((uint8)U->Mode > (uint8)SPIDRV_MODE_INTERRUPT) ||
            ((uint8)U->Role > (uint8)SPIDRV_ROLE_SLAVE))
        {
            return SPIDRV_BAD_PARAM;
        }
        Map[U->Instance] = i;
    }
    for (i = 0U; i < Cfg->DeviceCount; i++)
    {
        const SpiDrv_DeviceConfigType *D = &Cfg->Devices[i];
        if ((D->UnitIdx >= Cfg->UnitCount) || (NULL_PTR == D->IpDevice))
        {
            return SPIDRV_BAD_PARAM;
        }
        if ((0U != (D->OverrideMask & SPIDRV_OVR_BUS)) &&
            ((0U == D->BaudHz) || (D->Cpol > 1U) || (D->Cpha > 1U) || (D->Pcs > 3U)))
        {
            return SPIDRV_BAD_PARAM;
        }
    }
    return SPIDRV_OK;
}

static SpiDrv_StatusType SpiDrv_GetDevice(SpiDrv_DeviceId Dev, const SpiDrv_DeviceConfigType **Cfg,
                                          uint8 *UnitIdx)
{
    if (NULL_PTR == SpiDrv_pCfg)
    {
        return SPIDRV_NOT_INIT;
    }
    if (Dev >= SpiDrv_pCfg->DeviceCount)
    {
        return SPIDRV_BAD_PARAM;
    }
    *Cfg = &SpiDrv_pCfg->Devices[Dev];
    *UnitIdx = (*Cfg)->UnitIdx;
    return SPIDRV_OK;
}

static SpiDrv_StatusType SpiDrv_AcquireUnit(uint8 UnitIdx, SpiDrv_UnitStatusType *Prev)
{
    SpiDrv_StatusType St = SPIDRV_OK;
    SpiDrv_Plat_EnterCritical();
    if (SPIDRV_UNIT_BUSY == SpiDrv_Unit[UnitIdx].Status)
    {
        St = SPIDRV_BUSY;
    }
    else
    {
        *Prev = SpiDrv_Unit[UnitIdx].Status;
        SpiDrv_Unit[UnitIdx].Status = SPIDRV_UNIT_BUSY;
    }
    SpiDrv_Plat_ExitCritical();
    return St;
}

static void SpiDrv_ReleaseUnit(uint8 UnitIdx, SpiDrv_UnitStatusType NewStatus)
{
    SpiDrv_Plat_EnterCritical();
    SpiDrv_Unit[UnitIdx].Status = NewStatus;
    SpiDrv_Plat_ExitCritical();
}

static boolean SpiDrv_IsRole(uint8 UnitIdx, SpiDrv_RoleType Role)
{
    return (SpiDrv_pCfg->Units[UnitIdx].Role == Role) ? TRUE : FALSE;
}

/* A command refused before it started must not turn the unit into FAULT. */
static SpiDrv_UnitStatusType SpiDrv_StatusAfter(SpiDrv_StatusType St, SpiDrv_UnitStatusType Prev)
{
    SpiDrv_UnitStatusType S;
    if (SPIDRV_OK == St)
    {
        S = SPIDRV_UNIT_IDLE;
    }
    else if ((SPIDRV_TIMEOUT == St) || (SPIDRV_FIFO_ERROR == St))
    {
        S = SPIDRV_UNIT_FAULT;
    }
    else
    {
        S = Prev;
    }
    return S;
}

/* ------------------------------------------------------------------------------------------ */
/*                                          lifecycle                                          */
/* ------------------------------------------------------------------------------------------ */
SpiDrv_StatusType SpiDrv_Init(const SpiDrv_ConfigType *Cfg)
{
    SpiDrv_StatusType St;
    uint8 Map[SPIDRV_INSTANCE_COUNT];
    uint8 i;
    uint8 Inited = 0U;

    if (NULL_PTR != SpiDrv_pCfg)
    {
        return SPIDRV_ALREADY_INIT;
    }
    St = SpiDrv_ValidateConfig(Cfg, Map);
    if (SPIDRV_OK != St)
    {
        SPIDRV_DET(SPIDRV_API_INIT, (uint8)St);
        return St;
    }

    /* 1. units: clock check, IP init, transfer mode, IRQ */
    for (i = 0U; (i < Cfg->UnitCount) && (SPIDRV_OK == St); i++)
    {
        const SpiDrv_UnitConfigType *U = &Cfg->Units[i];
        uint32 Hz = SpiDrv_Plat_GetFuncClockHz(U->Instance);

        if (0U == Hz)
        {
            St = SPIDRV_CLOCK_ERROR;
        }
        else
        {
            St = SpiDrv_Be_UnitInit(U);
        }
        if (SPIDRV_OK == St)
        {
            Inited++;
            SpiDrv_Unit[i].Mode        = U->Mode;
            SpiDrv_Unit[i].FuncClockHz = Hz;
            SpiDrv_Unit[i].LastError   = SPIDRV_OK;
            SpiDrv_Unit[i].Callback    = NULL_PTR;
            SpiDrv_Unit[i].UserData    = NULL_PTR;
            SpiDrv_Unit[i].ActiveDev   = 0U;
            St = SpiDrv_Be_SetMode(U->Instance, U->Mode);
            if ((SPIDRV_OK == St) && (SPIDRV_MODE_INTERRUPT == U->Mode))
            {
                SpiDrv_Plat_IrqEnable(U->Instance, U->IrqPriority);
            }
        }
    }

    /* 2. devices */
    for (i = 0U; (i < Cfg->DeviceCount) && (SPIDRV_OK == St); i++)
    {
        const SpiDrv_DeviceConfigType *D = &Cfg->Devices[i];
        const SpiDrv_UnitConfigType   *U = &Cfg->Units[D->UnitIdx];
        St = SpiDrv_Be_DeviceInit(i, D, U->Instance, (SPIDRV_ROLE_MASTER == U->Role) ? TRUE : FALSE,
                                  SpiDrv_Unit[D->UnitIdx].FuncClockHz);
    }

    if (SPIDRV_OK != St)
    {
        for (i = 0U; i < Inited; i++)           /* roll back */
        {
            SpiDrv_Plat_IrqDisable(Cfg->Units[i].Instance);
            (void)SpiDrv_Be_UnitDeInit(Cfg->Units[i].Instance);
            SpiDrv_Unit[i].Status = SPIDRV_UNIT_UNINIT;
        }
        SPIDRV_DET(SPIDRV_API_INIT, (uint8)St);
        return St;
    }

    for (i = 0U; i < SPIDRV_INSTANCE_COUNT; i++) { SpiDrv_InstToUnit[i] = Map[i]; }
    for (i = 0U; i < Cfg->UnitCount; i++)        { SpiDrv_Unit[i].Status = SPIDRV_UNIT_IDLE; }
    SpiDrv_pCfg = Cfg;
    return SPIDRV_OK;
}

SpiDrv_StatusType SpiDrv_DeInit(void)
{
    uint8 i;
    SpiDrv_StatusType St = SPIDRV_OK;

    if (NULL_PTR == SpiDrv_pCfg)
    {
        return SPIDRV_NOT_INIT;
    }
    for (i = 0U; i < SpiDrv_pCfg->UnitCount; i++)
    {
        uint8 Inst = SpiDrv_pCfg->Units[i].Instance;
        if (SPIDRV_UNIT_BUSY == SpiDrv_Unit[i].Status)
        {
            SpiDrv_Be_Cancel(Inst);
        }
        SpiDrv_Plat_IrqDisable(Inst);
        if (SPIDRV_OK != SpiDrv_Be_UnitDeInit(Inst))
        {
            St = SPIDRV_BAD_PARAM;
        }
        SpiDrv_Unit[i].Status   = SPIDRV_UNIT_UNINIT;
        SpiDrv_Unit[i].Callback = NULL_PTR;
    }
    SpiDrv_pCfg = NULL_PTR;
    return St;
}

/* ------------------------------------------------------------------------------------------ */
/*                                          transfers                                          */
/* ------------------------------------------------------------------------------------------ */
SpiDrv_StatusType SpiDrv_SyncTransfer(SpiDrv_DeviceId Dev, const uint8 *Tx, uint8 *Rx,
                                      uint16 Length, uint32 TimeoutUs)
{
    const SpiDrv_DeviceConfigType *D;
    uint8 U = 0U;
    SpiDrv_UnitStatusType Prev = SPIDRV_UNIT_IDLE;
    SpiDrv_StatusType St = SpiDrv_GetDevice(Dev, &D, &U);

    if (SPIDRV_OK == St)
    {
        if ((0U == Length) || (0U == TimeoutUs) || ((NULL_PTR == Tx) && (NULL_PTR == Rx)))
        {
            St = SPIDRV_BAD_PARAM;
        }
    }
    if (SPIDRV_OK == St) { St = SpiDrv_AcquireUnit(U, &Prev); }
    if (SPIDRV_OK == St)
    {
        St = SpiDrv_Be_Sync(Dev, Tx, Rx, Length, TimeoutUs);
        if (SPIDRV_TIMEOUT == St)
        {
            SpiDrv_Be_Cancel(SpiDrv_pCfg->Units[U].Instance);
        }
        SpiDrv_Unit[U].LastError = St;
        SpiDrv_ReleaseUnit(U, SpiDrv_StatusAfter(St, Prev));
    }
    if ((SPIDRV_OK != St) && (SPIDRV_NOT_INIT != St)) { SPIDRV_DET(SPIDRV_API_SYNC, (uint8)St); }
    return St;
}

SpiDrv_StatusType SpiDrv_AsyncTransfer(SpiDrv_DeviceId Dev, const uint8 *Tx, uint8 *Rx,
                                       uint16 Length, SpiDrv_CallbackType Callback, void *UserData)
{
    const SpiDrv_DeviceConfigType *D;
    uint8 U = 0U;
    SpiDrv_UnitStatusType Prev = SPIDRV_UNIT_IDLE;
    SpiDrv_StatusType St = SpiDrv_GetDevice(Dev, &D, &U);

    if ((SPIDRV_OK == St) && ((0U == Length) || ((NULL_PTR == Tx) && (NULL_PTR == Rx))))
    {
        St = SPIDRV_BAD_PARAM;
    }
    if (SPIDRV_OK == St) { St = SpiDrv_AcquireUnit(U, &Prev); }
    if (SPIDRV_OK == St)
    {
        /* Unit is BUSY: safe to publish callback data before the IP can raise an interrupt. */
        SpiDrv_Unit[U].Callback  = Callback;
        SpiDrv_Unit[U].UserData  = UserData;
        SpiDrv_Unit[U].ActiveDev = Dev;
        St = SpiDrv_Be_Async(Dev, Tx, Rx, Length);
        if (SPIDRV_OK != St)
        {
            SpiDrv_Unit[U].Callback  = NULL_PTR;
            SpiDrv_Unit[U].LastError = St;
            SpiDrv_ReleaseUnit(U, SpiDrv_StatusAfter(St, Prev));
        }
    }
    if ((SPIDRV_OK != St) && (SPIDRV_NOT_INIT != St)) { SPIDRV_DET(SPIDRV_API_ASYNC, (uint8)St); }
    return St;
}

void SpiDrv_OnTransferEnd(uint8 Instance, SpiDrv_StatusType Result)
{
    uint8 U;
    SpiDrv_CallbackType Cb;
    void *User;
    SpiDrv_DeviceId Dev;

    if ((NULL_PTR == SpiDrv_pCfg) || (Instance >= SPIDRV_INSTANCE_COUNT))
    {
        return;
    }
    U = SpiDrv_InstToUnit[Instance];
    if (SPIDRV_INVALID_UNIT == U)
    {
        return;
    }
    SpiDrv_Plat_EnterCritical();
    if (SPIDRV_UNIT_BUSY != SpiDrv_Unit[U].Status)      /* cancelled meanwhile */
    {
        SpiDrv_Plat_ExitCritical();
        return;
    }
    Cb   = SpiDrv_Unit[U].Callback;
    User = SpiDrv_Unit[U].UserData;
    Dev  = SpiDrv_Unit[U].ActiveDev;
    SpiDrv_Unit[U].Callback  = NULL_PTR;
    SpiDrv_Unit[U].LastError = Result;
    SpiDrv_Unit[U].Status    = (SPIDRV_OK == Result) ? SPIDRV_UNIT_IDLE : SPIDRV_UNIT_FAULT;
    SpiDrv_Plat_ExitCritical();

    if (NULL_PTR != Cb)
    {
        Cb(Dev, (SPIDRV_OK == Result) ? SPIDRV_EVT_DONE : SPIDRV_EVT_ERROR, Result, User);
    }
}

SpiDrv_StatusType SpiDrv_Cancel(SpiDrv_DeviceId Dev)
{
    const SpiDrv_DeviceConfigType *D;
    uint8 U = 0U;
    SpiDrv_StatusType St = SpiDrv_GetDevice(Dev, &D, &U);
    SpiDrv_CallbackType Cb = NULL_PTR;
    void *User = NULL_PTR;
    SpiDrv_DeviceId Active = 0U;

    if (SPIDRV_OK == St)
    {
        SpiDrv_Plat_EnterCritical();
        if (SPIDRV_UNIT_BUSY == SpiDrv_Unit[U].Status)
        {
            Cb     = SpiDrv_Unit[U].Callback;
            User   = SpiDrv_Unit[U].UserData;
            Active = SpiDrv_Unit[U].ActiveDev;
            SpiDrv_Unit[U].Callback  = NULL_PTR;
            SpiDrv_Unit[U].LastError = SPIDRV_ABORTED;
            SpiDrv_Unit[U].Status    = SPIDRV_UNIT_IDLE;
            SpiDrv_Plat_ExitCritical();
            SpiDrv_Be_Cancel(SpiDrv_pCfg->Units[U].Instance);
            if (NULL_PTR != Cb)
            {
                Cb(Active, SPIDRV_EVT_ERROR, SPIDRV_ABORTED, User);
            }
        }
        else
        {
            SpiDrv_Plat_ExitCritical();
        }
    }
    return St;
}

void SpiDrv_MainFunction(void)
{
    uint8 i;
    if (NULL_PTR != SpiDrv_pCfg)
    {
        for (i = 0U; i < SpiDrv_pCfg->UnitCount; i++)
        {
            if ((SPIDRV_MODE_POLLING == SpiDrv_Unit[i].Mode) &&
                (SPIDRV_UNIT_BUSY == SpiDrv_Unit[i].Status))
            {
                SpiDrv_Be_Manage(SpiDrv_pCfg->Units[i].Instance);   /* may call SpiDrv_OnTransferEnd */
            }
        }
    }
}

/* ------------------------------------------------------------------------------------------ */
/*                                           status                                            */
/* ------------------------------------------------------------------------------------------ */
SpiDrv_UnitStatusType SpiDrv_GetUnitStatus(uint8 UnitIdx)
{
    return ((NULL_PTR == SpiDrv_pCfg) || (UnitIdx >= SpiDrv_pCfg->UnitCount))
           ? SPIDRV_UNIT_UNINIT : SpiDrv_Unit[UnitIdx].Status;
}

SpiDrv_StatusType SpiDrv_GetLastError(uint8 UnitIdx)
{
    return ((NULL_PTR == SpiDrv_pCfg) || (UnitIdx >= SpiDrv_pCfg->UnitCount))
           ? SPIDRV_NOT_INIT : SpiDrv_Unit[UnitIdx].LastError;
}

SpiDrv_StatusType SpiDrv_ClearFault(uint8 UnitIdx)
{
    SpiDrv_StatusType St = SPIDRV_OK;
    if ((NULL_PTR == SpiDrv_pCfg) || (UnitIdx >= SpiDrv_pCfg->UnitCount))
    {
        St = SPIDRV_NOT_INIT;
    }
    else
    {
        SpiDrv_Plat_EnterCritical();
        if (SPIDRV_UNIT_FAULT == SpiDrv_Unit[UnitIdx].Status)
        {
            SpiDrv_Unit[UnitIdx].Status    = SPIDRV_UNIT_IDLE;
            SpiDrv_Unit[UnitIdx].LastError = SPIDRV_OK;
        }
        else if (SPIDRV_UNIT_BUSY == SpiDrv_Unit[UnitIdx].Status)
        {
            St = SPIDRV_BUSY;
        }
        else
        {
            /* IDLE: nothing to clear */
        }
        SpiDrv_Plat_ExitCritical();
    }
    return St;
}

/* ------------------------------------------------------------------------------------------ */
/*                                     runtime changes                                         */
/* ------------------------------------------------------------------------------------------ */
SpiDrv_StatusType SpiDrv_SetBaud(SpiDrv_DeviceId Dev, uint32 BaudHz, uint32 *ActualHz)
{
    const SpiDrv_DeviceConfigType *D;
    uint8 U = 0U;
    SpiDrv_UnitStatusType Prev = SPIDRV_UNIT_IDLE;
    SpiDrv_StatusType St = SpiDrv_GetDevice(Dev, &D, &U);

    if (SPIDRV_OK == St)
    {
        if (0U == BaudHz)                                     { St = SPIDRV_BAD_PARAM; }
        else if (0U == (D->OverrideMask & SPIDRV_OVR_BUS))   { St = SPIDRV_NOT_SUPPORTED; }
        else                                                  { St = SpiDrv_AcquireUnit(U, &Prev); }
    }
    if (SPIDRV_OK == St)
    {
        uint32 Hz = SpiDrv_Plat_GetFuncClockHz(SpiDrv_pCfg->Units[U].Instance);
        if (0U == Hz)
        {
            St = SPIDRV_CLOCK_ERROR;
        }
        else
        {
            SpiDrv_Unit[U].FuncClockHz = Hz;
            St = SpiDrv_Be_SetBaud(Dev, D, SpiDrv_IsRole(U, SPIDRV_ROLE_MASTER), Hz, BaudHz, ActualHz);
        }
        SpiDrv_ReleaseUnit(U, Prev);
    }
    return St;
}

/* Common wrapper for parameters changed through the IP Update* functions. */
typedef enum { PARAM_FRAME, PARAM_LSB, PARAM_DEFAULT } SpiDrv_ParamKind;

static SpiDrv_StatusType SpiDrv_SetParam(SpiDrv_DeviceId Dev, SpiDrv_ParamKind Kind, uint32 Value)
{
    const SpiDrv_DeviceConfigType *D;
    uint8 U = 0U;
    SpiDrv_UnitStatusType Prev = SPIDRV_UNIT_IDLE;
    SpiDrv_StatusType St = SpiDrv_GetDevice(Dev, &D, &U);

    if (SPIDRV_OK == St) { St = SpiDrv_AcquireUnit(U, &Prev); }
    if (SPIDRV_OK == St)
    {
        switch (Kind)
        {
            case PARAM_FRAME:   St = SpiDrv_Be_SetFrameSize(Dev, (uint8)Value); break;
            case PARAM_LSB:     St = SpiDrv_Be_SetLsb(Dev, (0U != Value) ? TRUE : FALSE); break;
            default:            St = SpiDrv_Be_SetDefaultData(Dev, Value); break;
        }
        SpiDrv_ReleaseUnit(U, Prev);
    }
    return St;
}

SpiDrv_StatusType SpiDrv_SetFrameSize(SpiDrv_DeviceId Dev, uint8 FrameBits)
{
    return SpiDrv_SetParam(Dev, PARAM_FRAME, (uint32)FrameBits);
}

SpiDrv_StatusType SpiDrv_SetLsbFirst(SpiDrv_DeviceId Dev, boolean LsbFirst)
{
    return SpiDrv_SetParam(Dev, PARAM_LSB, (TRUE == LsbFirst) ? 1U : 0U);
}

SpiDrv_StatusType SpiDrv_SetDefaultData(SpiDrv_DeviceId Dev, uint32 DefaultData)
{
    return SpiDrv_SetParam(Dev, PARAM_DEFAULT, DefaultData);
}

SpiDrv_StatusType SpiDrv_SetMode(uint8 UnitIdx, SpiDrv_ModeType Mode)
{
    SpiDrv_UnitStatusType Prev = SPIDRV_UNIT_IDLE;
    SpiDrv_StatusType St = SPIDRV_OK;

    if (NULL_PTR == SpiDrv_pCfg)                          { St = SPIDRV_NOT_INIT; }
    else if ((UnitIdx >= SpiDrv_pCfg->UnitCount) ||
             ((uint8)Mode > (uint8)SPIDRV_MODE_INTERRUPT)) { St = SPIDRV_BAD_PARAM; }
    else                                                   { St = SpiDrv_AcquireUnit(UnitIdx, &Prev); }

    if (SPIDRV_OK == St)
    {
        uint8 Inst = SpiDrv_pCfg->Units[UnitIdx].Instance;
        St = SpiDrv_Be_SetMode(Inst, Mode);
        if (SPIDRV_OK == St)
        {
            SpiDrv_Unit[UnitIdx].Mode = Mode;
            if (SPIDRV_MODE_INTERRUPT == Mode)
            {
                SpiDrv_Plat_IrqEnable(Inst, SpiDrv_pCfg->Units[UnitIdx].IrqPriority);
            }
            else
            {
                SpiDrv_Plat_IrqDisable(Inst);
            }
        }
        SpiDrv_ReleaseUnit(UnitIdx, Prev);
    }
    return St;
}
