#include "can_app.h"
#include <stddef.h>
#include <string.h>

#define CAN_APP_TELEMETRY_PERIOD_MS UINT32_C(100)
static Can_Frame inflight;
static Cp_Command reply_command;
static Cp_Result reply_code;
static bool reply_due, status_due, hb_due, error_due;
static bool tx_pending, tx_failed, clock_seen, telemetry_started;
static uint32_t last_telemetry;
volatile CanApp_Diagnostics g_CanAppDiagnostics;
volatile Can_Diagnostics g_CanDriverDiagnostics;

static void Fault(Cp_Fault fault)
{
    AppShared_LatchInhibit(fault);
    if (g_CanAppDiagnostics.fault==CP_FAULT_NONE) {
        g_CanAppDiagnostics.fault=fault;
        error_due=true;
    }
}
bool CanApp_LocalStartAllowed(uint32_t now_ms, bool time_valid)
{
    (void)now_ms;
    return time_valid && g_CanAppDiagnostics.fault==CP_FAULT_NONE;
}
void CanApp_Init(const CanApp_Config *config)
{
    (void)config; /* Retain main.c's API; the old timeout has no effect in V2. */
    memset(&inflight,0,sizeof inflight);
    memset(&reply_command,0,sizeof reply_command);
    memset((void *)&g_CanAppDiagnostics,0,sizeof g_CanAppDiagnostics);
    memset((void *)&g_CanDriverDiagnostics,0,sizeof g_CanDriverDiagnostics);
    reply_due=false; status_due=false; hb_due=false; error_due=false;
    tx_pending=false; tx_failed=false; clock_seen=false; telemetry_started=false;
    last_telemetry=0U;
}
static void Reply(const Cp_Command *command, Cp_Result code)
{
    /* Latest completion only. A busy CAN TX must never block motor control. */
    reply_command=*command;
    reply_code=code;
    reply_due=true;
}
static void Receive(const Can_Frame *frame)
{
    Cp_DecodeResult decoded=CanProtocol_Decode(frame->id,false,false,frame->dlc,frame->data);
    uint32_t id;
    Cp_Result result;
    if (!decoded.valid) {
        ++g_CanAppDiagnostics.malformed;
        if (decoded.respond) Reply(&decoded.command,decoded.result);
        return;
    }
    if (decoded.command.opcode==CP_GET_STATUS) {
        status_due=true;
        hb_due=true;
        Reply(&decoded.command,CP_OK);
        return;
    }
    result=decoded.command.opcode==CP_STOP ?
        AppShared_RequestStop(&decoded.command,&id) :
        AppShared_PublishCommand(&decoded.command,&id);
    if (result!=CP_PENDING) Reply(&decoded.command,result);
}
static void Results(void)
{
    App_Command command;
    App_Result result;
    if (!AppShared_ReadResult(&command,&result)) return;
    if (command.command.opcode==CP_CLEAR_FAULT && result.code==CP_COMPLETED) {
        g_CanAppDiagnostics.fault=CP_FAULT_NONE;
        error_due=false;
        tx_failed=false;
    }
    Reply(&command.command,result.code);
    (void)AppShared_AcknowledgeResult(result.request_id);
}
static void EncodeSnapshot(bool error, bool valid)
{
    App_MotorStatus motor=AppShared_ReadSnapshot().motor;
    Cp_Fault fault=g_CanAppDiagnostics.fault;
    uint8_t flags=0U;
    if (motor.backend_ready) flags|=CP_FLAG_CONFIGURED;
    if (motor.safe_off_confirmed) flags|=CP_FLAG_SAFE_OFF;
    if (motor.reverse_supported) flags|=CP_FLAG_REVERSE_SUPPORTED;
    if (motor.reverse) flags|=CP_FLAG_REVERSE;
    if (AppShared_HasPending()) flags|=CP_FLAG_OPERATION_PENDING;
    if (valid && motor.backend_ready && motor.fault==CP_FAULT_NONE &&
        fault==CP_FAULT_NONE && !AppShared_ReadInhibit().latched)
        flags|=CP_FLAG_CONTROL_ALLOWED;
    if (fault==CP_FAULT_NONE) fault=motor.fault;
    if (error) CanProtocol_EncodeError(inflight.data,flags,motor.requested_duty,fault);
    else CanProtocol_EncodeStatus(inflight.data,motor.state,flags,motor.requested_duty,fault);
}
static void EncodeHbSnapshot(void)
{
    App_MotorStatus motor=AppShared_ReadSnapshot().motor;
    CanProtocol_EncodeHbDiagnostics(inflight.data,motor.hb_valid,motor.hb_id,
                                   motor.hb_status,motor.hb_config);
}
static void Transmit(uint32_t now, bool valid, bool can_send)
{
    Can_Status result;
    bool *due;
    if (valid && AppShared_ReadSnapshot().motor.valid) {
        if (!telemetry_started) { last_telemetry=now; telemetry_started=true; }
        if ((uint32_t)(now-last_telemetry)>=CAN_APP_TELEMETRY_PERIOD_MS &&
            !status_due && !hb_due) {
            last_telemetry=now;
            status_due=true;
            hb_due=true;
        }
    }
    if (tx_failed) return;
    if (tx_pending) {
        if (CanDriver_GetState()!=CAN_DRIVER_RUNNING) return;
        result=CanDriver_PollTx();
        g_CanAppDiagnostics.last_api_status=result;
        if (result==CAN_STATUS_BUSY) return;
        if (result!=CAN_STATUS_OK) { tx_failed=true; Fault(CP_FAULT_CAN_API); return; }
        tx_pending=false;
    }
    if (!can_send || CanDriver_GetState()!=CAN_DRIVER_RUNNING) return;
    inflight.id=CP_RESPONSE_ID;
    inflight.dlc=CP_DLC;
    if (reply_due) {
        CanProtocol_EncodeAck(inflight.data,&reply_command,reply_code,g_CanAppDiagnostics.fault);
        due=&reply_due;
    } else if (error_due) {
        EncodeSnapshot(true,valid);
        due=&error_due;
    } else if (status_due) {
        EncodeSnapshot(false,valid);
        due=&status_due;
    } else if (hb_due) {
        EncodeHbSnapshot();
        due=&hb_due;
    } else return;
    result=CanDriver_Transmit(&inflight);
    g_CanAppDiagnostics.last_api_status=result;
    if (result==CAN_STATUS_BUSY) return;
    if (result!=CAN_STATUS_OK) { tx_failed=true; Fault(CP_FAULT_CAN_API); return; }
    *due=false;
    tx_pending=true;
}
void CanApp_Service(uint32_t now, bool valid)
{
    Can_Diagnostics diagnostics={0};
    Can_Frame received;
    Can_Status result;
    bool can_send=true;
    App_Inhibit safety;
    ++g_CanAppDiagnostics.service_cycles;
    Results(); /* Consumes completions even when the USB-CAN cannot receive TX. */
    safety=AppShared_ReadInhibit();
    if (safety.fault!=CP_FAULT_NONE && g_CanAppDiagnostics.fault==CP_FAULT_NONE)
        Fault(safety.fault);
    if (valid) clock_seen=true;
    else if (clock_seen) Fault(CP_FAULT_TIME);
    if (CanDriver_GetState()==CAN_DRIVER_RUNNING) {
        result=CanDriver_PollDiagnostics();
        g_CanAppDiagnostics.last_api_status=result;
        if (result!=CAN_STATUS_OK) { Fault(CP_FAULT_CAN_API); can_send=false; }
    }
    if (CanDriver_GetDiagnostics(&diagnostics)!=CAN_STATUS_OK) {
        Fault(CP_FAULT_CAN_API); can_send=false;
    } else g_CanDriverDiagnostics=diagnostics;
    if (diagnostics.bus_off_seen) { Fault(CP_FAULT_BUS_OFF); can_send=false; }
    if (CanDriver_GetState()!=CAN_DRIVER_RUNNING) { Fault(CP_FAULT_CAN_API); can_send=false; }
    if (CanDriver_GetState()==CAN_DRIVER_RUNNING) {
        result=CanDriver_PollRx(&received);
        g_CanAppDiagnostics.last_api_status=result;
        if (result==CAN_STATUS_RX_OVERRUN) {
            ++g_CanAppDiagnostics.rx_overruns;
            Fault(CP_FAULT_RX_OVERRUN);
        }
        if (result==CAN_STATUS_OK || result==CAN_STATUS_RX_OVERRUN) Receive(&received);
        else if (result==CAN_STATUS_INVALID_FRAME) ++g_CanAppDiagnostics.malformed;
        else if (result!=CAN_STATUS_NO_DATA) Fault(CP_FAULT_CAN_API);
    }
    Transmit(now,valid,can_send);
    g_CanAppDiagnostics.tx_pending=tx_pending;
    g_CanAppDiagnostics.tx_failed=tx_failed;
    g_CanAppDiagnostics.control_allowed=CanApp_LocalStartAllowed(now,valid);
}
