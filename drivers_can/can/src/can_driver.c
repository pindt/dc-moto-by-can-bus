#include <can_cfg.h>
#include <can_driver.h>
#include "FlexCAN_Ip.h"
#include "FlexCAN_Ip_HwAccess.h" /* RTD receive metadata masks only. */
#include "IntCtrl_Ip.h"

#include <stdbool.h>
#include <stddef.h>

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE != STD_OFF)
#error "Phase 1 CAN adapter requires the existing DMA-disabled RTD configuration"
#endif

/* Generated FlexCAN_State0 remains the RTD controller state. Only private
 * transfer storage is added; application/SPI headers see no RTD objects.
 */
static Can_DriverState s_state = CAN_DRIVER_UNINITIALIZED;
static Can_Config s_config;
static Flexcan_Ip_ConfigType s_rtd_config;
static Can_Diagnostics s_diagnostics;
static bool s_rtd_initialized;
static bool s_diagnostic_access; /* Module running, not stopped/being deinitialized. */
static bool s_tx_pending;
static Flexcan_Ip_MsgBuffType s_rx_buffer;
/* RTD reads TX through uint32 pointers on S32K144; guarantee alignment. */
static union
{
    uint32 words[2];
    uint8 bytes[8];
} s_tx_buffer;

static Can_Status Can_MapStatus(Flexcan_Ip_StatusType status)
{
    switch (status)
    {
        case FLEXCAN_STATUS_SUCCESS: return CAN_STATUS_OK;
        case FLEXCAN_STATUS_BUSY: return CAN_STATUS_BUSY;
        case FLEXCAN_STATUS_TIMEOUT: return CAN_STATUS_TIMEOUT;
        case FLEXCAN_STATUS_NO_TRANSFER_IN_PROGRESS: return CAN_STATUS_NO_TRANSFER;
        case FLEXCAN_STATUS_BUFF_OUT_OF_RANGE: return CAN_STATUS_CONFIG_ERROR;
        case FLEXCAN_STATUS_ERROR:
        default: return CAN_STATUS_HW_ERROR;
    }
}

/* Device masks decode RTD return values; no CAN register access here. */
#define CAN_ERROR_EVENTS (FLEXCAN_ESR1_ACKERR_MASK | FLEXCAN_ESR1_BIT0ERR_MASK | \
    FLEXCAN_ESR1_BIT1ERR_MASK | FLEXCAN_ESR1_CRCERR_MASK | \
    FLEXCAN_ESR1_FRMERR_MASK | FLEXCAN_ESR1_STFERR_MASK | \
    FLEXCAN_ESR1_ERRINT_MASK | FLEXCAN_ESR1_BOFFINT_MASK | \
    FLEXCAN_ESR1_TWRNINT_MASK | FLEXCAN_ESR1_RWRNINT_MASK | FLEXCAN_ESR1_ERROVR_MASK)

static void Can_RecordFault(Can_Status status)
{
    if (s_diagnostics.first_fault_valid == 0U)
    {
        s_diagnostics.first_fault_valid = 1U;
        s_diagnostics.first_fault_status = status;
        s_diagnostics.first_fault_sample_valid = s_diagnostics.valid;
        s_diagnostics.first_fault = s_diagnostics.latest;
    }
}

static void Can_CaptureDiagnostics(void)
{
    Can_DiagnosticSample sample = {0};
    uint32 fault;
    if (!s_rtd_initialized || !s_diagnostic_access)
    {
        return;
    }
    sample.esr1 = FlexCAN_Ip_GetErrorStatus(CAN_CFG_INSTANCE);
    sample.tx_error_counter = FlexCAN_Ip_GetControllerTxErrorCounter(CAN_CFG_INSTANCE);
    sample.rx_error_counter = FlexCAN_Ip_GetControllerRxErrorCounter(CAN_CFG_INSTANCE);
    fault = (sample.esr1 & FLEXCAN_ESR1_FLTCONF_MASK) >> FLEXCAN_ESR1_FLTCONF_SHIFT;
    sample.fault_state = (fault >= 2U) ? CAN_FAULT_BUS_OFF :
                         ((fault == 1U) ? CAN_FAULT_PASSIVE : CAN_FAULT_ACTIVE);
    sample.tx_warning = ((sample.esr1 & FLEXCAN_ESR1_TXWRN_MASK) != 0U);
    sample.rx_warning = ((sample.esr1 & FLEXCAN_ESR1_RXWRN_MASK) != 0U);
    sample.ack_error = ((sample.esr1 & FLEXCAN_ESR1_ACKERR_MASK) != 0U);
    sample.bit0_error = ((sample.esr1 & FLEXCAN_ESR1_BIT0ERR_MASK) != 0U);
    sample.bit1_error = ((sample.esr1 & FLEXCAN_ESR1_BIT1ERR_MASK) != 0U);
    sample.crc_error = ((sample.esr1 & FLEXCAN_ESR1_CRCERR_MASK) != 0U);
    sample.form_error = ((sample.esr1 & FLEXCAN_ESR1_FRMERR_MASK) != 0U);
    sample.stuff_error = ((sample.esr1 & FLEXCAN_ESR1_STFERR_MASK) != 0U);
    s_diagnostics.latest = sample;
    s_diagnostics.valid = 1U;
    if (s_diagnostics.sample_count != UINT32_MAX)
    {
        ++s_diagnostics.sample_count;
    }
    s_diagnostics.accumulated_error_events |= sample.esr1 & CAN_ERROR_EVENTS;
    if ((s_diagnostics.first_error_valid == 0U) &&
        (((sample.esr1 & CAN_ERROR_EVENTS) != 0U) ||
         (sample.fault_state != CAN_FAULT_ACTIVE) || sample.tx_warning || sample.rx_warning))
    {
        s_diagnostics.first_error_valid = 1U;
        s_diagnostics.first_error = sample;
    }
    if ((sample.fault_state == CAN_FAULT_BUS_OFF) ||
        ((sample.esr1 & FLEXCAN_ESR1_BOFFINT_MASK) != 0U))
    {
        s_diagnostics.bus_off_seen = 1U;
        Can_RecordFault(CAN_STATUS_HW_ERROR);
    }
}

static Can_Status Can_Fail(Flexcan_Ip_StatusType status)
{
    Can_CaptureDiagnostics();
    Can_RecordFault(Can_MapStatus(status));
    s_state = CAN_DRIVER_FAULT;
    return Can_MapStatus(status);
}

static void Can_DisableNvic(void)
{
    static const IRQn_Type irqs[] = {
        CAN0_ORed_IRQn, CAN0_Error_IRQn, CAN0_Wake_Up_IRQn,
        CAN0_ORed_0_15_MB_IRQn, CAN0_ORed_16_31_MB_IRQn
    };
    size_t index;
    for (index = 0U; index < (sizeof(irqs) / sizeof(irqs[0])); ++index)
    {
        IntCtrl_Ip_DisableIrq(irqs[index]);
        IntCtrl_Ip_ClearPending(irqs[index]);
    }
}

/* Reject incompatible regenerated settings instead of silently enabling a
 * different transport or callbacks. This does not measure the actual clock.
 */
static bool Can_BaselineMatches(void)
{
    return (INST_FLEXCAN_0 == CAN_CFG_INSTANCE) &&
           (FlexCAN_Config0.max_num_mb == 16U) &&
           ((FlexCAN_Config0.flexcanMode == FLEXCAN_LOOPBACK_MODE)||
        	(FlexCAN_Config0.flexcanMode == FLEXCAN_NORMAL_MODE)) &&
           (FlexCAN_Config0.is_rx_fifo_needed == FALSE) &&
           (FlexCAN_Config0.fd_enable == FALSE) &&
           (FlexCAN_Config0.bitRateSwitch == FALSE) &&
           (FlexCAN_Config0.is_pe_clock == TRUE) &&
           (FlexCAN_Config0.payload.payloadBlock0 == FLEXCAN_PAYLOAD_SIZE_8) &&
           (FlexCAN_Config0.enhCbtEnable == FALSE) &&
           (FlexCAN_Config0.Callback == NULL_PTR) &&
           (FlexCAN_Config0.ErrorCallback == NULL_PTR) &&
           (FlexCAN_Config0.bitrate.propSeg == 7U) &&
           (FlexCAN_Config0.bitrate.phaseSeg1 == 4U) &&
           (FlexCAN_Config0.bitrate.phaseSeg2 == 1U) &&
           (FlexCAN_Config0.bitrate.preDivider == 5U) &&
           (FlexCAN_Config0.bitrate.rJumpwidth == 1U);
}

Can_Status CanDriver_Init(const Can_Config *config)
{
    Flexcan_Ip_StatusType status;
    if ((config == NULL) || (config->rx_id > CAN_CFG_MAX_STANDARD_ID) ||
        ((config->mode != CAN_MODE_LOOPBACK) && (config->mode != CAN_MODE_NORMAL)))
    {
        return CAN_STATUS_INVALID_ARGUMENT;
    }
    if (s_state != CAN_DRIVER_UNINITIALIZED)
    {
        return CAN_STATUS_BAD_STATE;
    }
    if (!Can_BaselineMatches())
    {
        return CAN_STATUS_CONFIG_ERROR;
    }

    /* Ownership is a caller precondition; the RTD exposes no safe public
     * "is initialized by another owner" query. No automatic takeover.
     */
    Can_DisableNvic();
    s_config = *config;
    s_rtd_config = FlexCAN_Config0;
    s_rtd_config.flexcanMode = (config->mode == CAN_MODE_NORMAL) ?
                              FLEXCAN_NORMAL_MODE : FLEXCAN_LOOPBACK_MODE;
    s_tx_pending = false;
    s_rtd_initialized = false;
    s_diagnostic_access = false;
    status = FlexCAN_Ip_Init(CAN_CFG_INSTANCE, &FlexCAN_State0, &s_rtd_config);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        /* RTD may not have published its state pointer. Never call transfer
         * APIs here. Deinit is the explicit recovery path for this checkout.
         */
        return Can_Fail(status);
    }
    /* Reset only after a fully successful Init, including IRQ suppression. */
    s_rtd_initialized = true;
    s_diagnostic_access = true;
    status = FlexCAN_Ip_DisableInterrupts(CAN_CFG_INSTANCE);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status);
    }
    {
        const Can_Diagnostics fresh = {0};
        s_diagnostics = fresh;
        s_diagnostics.mode = s_config.mode;
    }
    s_state = CAN_DRIVER_READY;
    return CAN_STATUS_OK;
}

Can_Status CanDriver_Start(void)
{
    Flexcan_Ip_StatusType status;
    const Flexcan_Ip_DataInfoType info = {
        .msg_id_type = FLEXCAN_MSG_ID_STD,
        .data_length = CAN_CFG_MAX_DLC,
        .is_remote = FALSE,
        .is_polling = TRUE
    };
    if (s_state != CAN_DRIVER_READY)
    {
        return CAN_STATUS_BAD_STATE;
    }
    /* RTD Stop leaves MDIS set. Enable before MB access on both the first
     * start and restart; loopback TX cannot start until Transmit is called.
     */
    status = FlexCAN_Ip_SetStartMode(CAN_CFG_INSTANCE);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status);
    }
    s_diagnostic_access = true;
    status = FlexCAN_Ip_ConfigRxMb(CAN_CFG_INSTANCE, CAN_CFG_RX_MB, &info,
                                  s_config.rx_id);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status);
    }
    status = FlexCAN_Ip_Receive(CAN_CFG_INSTANCE, CAN_CFG_RX_MB, &s_rx_buffer, TRUE);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status);
    }
    s_state = CAN_DRIVER_RUNNING;
    return CAN_STATUS_OK;
}

Can_Status CanDriver_Transmit(const Can_Frame *frame)
{
    Flexcan_Ip_StatusType status;
    uint8_t index;
    Flexcan_Ip_DataInfoType info = {
        .msg_id_type = FLEXCAN_MSG_ID_STD,
        .is_remote = FALSE,
        .is_polling = TRUE
    };
    if ((frame == NULL) || (frame->id > CAN_CFG_MAX_STANDARD_ID) ||
        (frame->dlc > CAN_CFG_MAX_DLC))
    {
        return CAN_STATUS_INVALID_ARGUMENT;
    }
    if (s_state != CAN_DRIVER_RUNNING)
    {
        return CAN_STATUS_BAD_STATE;
    }
    if (s_tx_pending)
    {
        return CAN_STATUS_BUSY;
    }
    info.data_length = frame->dlc;
    for (index = 0U; index < CAN_CFG_MAX_DLC; ++index)
    {
        s_tx_buffer.bytes[index] = (index < frame->dlc) ? frame->data[index] : 0U;
    }
    status = FlexCAN_Ip_Send(CAN_CFG_INSTANCE, CAN_CFG_TX_MB, &info,
                            frame->id, s_tx_buffer.bytes);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        /* An unexpected RTD BUSY also faults ownership; do not service or
         * overwrite an unknown transaction as if it were our accepted frame.
         */
        return Can_Fail(status);
    }
    s_tx_pending = true;
    return CAN_STATUS_OK;
}

Can_Status CanDriver_PollTx(void)
{
    Flexcan_Ip_StatusType status;
    if (s_state != CAN_DRIVER_RUNNING)
    {
        return CAN_STATUS_BAD_STATE;
    }
    if (!s_tx_pending)
    {
        return CAN_STATUS_NO_TRANSFER;
    }
    FlexCAN_Ip_MainFunctionWrite(CAN_CFG_INSTANCE, CAN_CFG_TX_MB);
    status = FlexCAN_Ip_GetTransferStatus(CAN_CFG_INSTANCE, CAN_CFG_TX_MB);
    if (status == FLEXCAN_STATUS_BUSY)
    {
        return CAN_STATUS_BUSY;
    }
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status);
    }
    s_tx_pending = false;
    return CAN_STATUS_OK;
}

Can_Status CanDriver_PollRx(Can_Frame *frame)
{
    Flexcan_Ip_StatusType status;
    Can_Status result = CAN_STATUS_OK;
    Can_Frame received = {0};
    uint32 code;
    uint8_t index;
    if (frame == NULL)
    {
        return CAN_STATUS_INVALID_ARGUMENT;
    }
    if (s_state != CAN_DRIVER_RUNNING)
    {
        return CAN_STATUS_BAD_STATE;
    }
    FlexCAN_Ip_MainFunctionRead(CAN_CFG_INSTANCE, CAN_CFG_RX_MB);
    status = FlexCAN_Ip_GetTransferStatus(CAN_CFG_INSTANCE, CAN_CFG_RX_MB);
    if (status == FLEXCAN_STATUS_BUSY)
    {
        return CAN_STATUS_NO_DATA;
    }
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status);
    }

    /* Decode only the metadata returned by RTD; no register access. */
    code = (s_rx_buffer.cs & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT;
    if ((s_rx_buffer.msgId > CAN_CFG_MAX_STANDARD_ID) ||
        (s_rx_buffer.dataLen > CAN_CFG_MAX_DLC) ||
        (((s_rx_buffer.cs & FLEXCAN_IP_CS_DLC_MASK) >> FLEXCAN_IP_CS_DLC_SHIFT) >
         CAN_CFG_MAX_DLC) ||
        ((s_rx_buffer.cs & (FLEXCAN_IP_CS_IDE_MASK | FLEXCAN_IP_CS_RTR_MASK |
                           FLEXCAN_IP_MB_EDL_MASK)) != 0U) ||
        ((code != (uint32)FLEXCAN_RX_FULL) && (code != (uint32)FLEXCAN_RX_OVERRUN)))
    {
        result = CAN_STATUS_INVALID_FRAME;
    }
    else
    {
        received.id = (uint16_t)s_rx_buffer.msgId;
        received.dlc = s_rx_buffer.dataLen;
        for (index = 0U; index < received.dlc; ++index)
        {
            received.data[index] = s_rx_buffer.data[index];
        }
        if (code == (uint32)FLEXCAN_RX_OVERRUN)
        {
            result = CAN_STATUS_RX_OVERRUN;
        }
    }
    /* RTD completion set RX software state IDLE. Re-register its persistent
     * destination every time, including after a rejected frame.
     */
    status = FlexCAN_Ip_Receive(CAN_CFG_INSTANCE, CAN_CFG_RX_MB, &s_rx_buffer, TRUE);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status); /* Output untouched; completed frame discarded. */
    }
    if ((result == CAN_STATUS_OK) || (result == CAN_STATUS_RX_OVERRUN))
    {
        *frame = received;
    }
    return result;
}

Can_Status CanDriver_Stop(void)
{
    Flexcan_Ip_StatusType status;
    if (s_state != CAN_DRIVER_RUNNING)
    {
        return CAN_STATUS_BAD_STATE;
    }
    if (s_tx_pending)
    {
        return CAN_STATUS_BUSY;
    }
    status = FlexCAN_Ip_AbortTransfer(CAN_CFG_INSTANCE, CAN_CFG_RX_MB);
    if ((status != FLEXCAN_STATUS_SUCCESS) &&
        (status != FLEXCAN_STATUS_NO_TRANSFER_IN_PROGRESS))
    {
        return Can_Fail(status);
    }
    /* Stop may leave MDIS set even on timeout; save evidence beforehand. */
    Can_CaptureDiagnostics();
    s_diagnostic_access = false;
    status = FlexCAN_Ip_SetStopMode(CAN_CFG_INSTANCE);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status);
    }
    s_state = CAN_DRIVER_READY;
    s_diagnostic_access = false;
    return CAN_STATUS_OK;
}

Can_Status CanDriver_Deinit(void)
{
    Flexcan_Ip_StatusType status;
    if ((s_state != CAN_DRIVER_READY) && (s_state != CAN_DRIVER_FAULT))
    {
        return CAN_STATUS_BAD_STATE;
    }
    /* This RTD Deinit can run even if Init failed before publishing state.
     * It ignores the internal Disable return; check GetStopMode afterwards.
     */
    /* Do not sample registers after Deinit may have disabled the module. */
    Can_CaptureDiagnostics();
    s_diagnostic_access = false;
    status = FlexCAN_Ip_Deinit(CAN_CFG_INSTANCE);
    if (status != FLEXCAN_STATUS_SUCCESS)
    {
        return Can_Fail(status);
    }
    if (FlexCAN_Ip_GetStopMode(CAN_CFG_INSTANCE) != TRUE)
    {
        return Can_Fail(FLEXCAN_STATUS_ERROR);
    }
    Can_DisableNvic();
    s_tx_pending = false;
    s_rtd_initialized = false;
    s_state = CAN_DRIVER_UNINITIALIZED;
    return CAN_STATUS_OK;
}

Can_DriverState CanDriver_GetState(void)
{
    return s_state;
}

Can_Status CanDriver_PollDiagnostics(void)
{
    if (s_state != CAN_DRIVER_RUNNING)
    {
        return CAN_STATUS_BAD_STATE;
    }
    Can_CaptureDiagnostics();
    return CAN_STATUS_OK;
}

Can_Status CanDriver_GetDiagnostics(Can_Diagnostics *out)
{
    if (out == NULL)
    {
        return CAN_STATUS_INVALID_ARGUMENT;
    }
    *out = s_diagnostics;
    out->driver_state = s_state;
    out->tx_pending = s_tx_pending ? 1U : 0U;
    return CAN_STATUS_OK;
}
