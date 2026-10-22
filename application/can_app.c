#include "can_app.h"
#include <stddef.h>
#include <string.h>

/* Semantic reply slots, not frame queues. GET_STATUS uses ACK then STATUS. */
typedef struct {
    Cp_Command command;
    Cp_Result code;
    bool ack_due, status_due;
} Reply;
typedef struct {
    Cp_Command command;
    Cp_Result code;
    uint32_t request_id;
    bool valid;
} LastRequest;
static Reply ordinary_reply, stop_reply;
static LastRequest last;
static Can_Frame inflight;
static uint32_t timeout_ms, last_keepalive;
static uint16_t max_duty;
static bool limit_valid, lease_seen, tx_pending, tx_failed, error_due;
volatile CanApp_Diagnostics g_CanAppDiagnostics;

static bool ReplyFree(const Reply *reply)
{ return !reply->ack_due && !reply->status_due; }
static bool SetReply(const Cp_Command *command, Cp_Result code)
{
    Reply *reply=command->opcode==CP_STOP ? &stop_reply : &ordinary_reply;
    if (!ReplyFree(reply)) return false;
    reply->command=*command;
    reply->code=code;
    reply->ack_due=true;
    reply->status_due=command->opcode==CP_GET_STATUS && code==CP_OK;
    return true;
}
static void Fault(Cp_Fault fault)
{
    AppShared_LatchInhibit(fault);
    if (g_CanAppDiagnostics.fault==CP_FAULT_NONE) {
        g_CanAppDiagnostics.fault=AppShared_ReadInhibit().fault;
        error_due=true;
    }
}
static bool LeaseFresh(uint32_t now, bool valid)
{
    return valid && lease_seen && timeout_ms!=0U &&
           (uint32_t)(now-last_keepalive)<timeout_ms;
}
void CanApp_Init(const CanApp_Config *config)
{
    memset(&ordinary_reply,0,sizeof ordinary_reply);
    memset(&stop_reply,0,sizeof stop_reply);
    memset(&last,0,sizeof last);
    memset(&inflight,0,sizeof inflight);
    memset((void *)&g_CanAppDiagnostics,0,sizeof g_CanAppDiagnostics);
    timeout_ms=config!=NULL ? config->communication_timeout_ms : 0U;
    last_keepalive=0U;
    max_duty=0U;
    limit_valid=false;
    lease_seen=false;
    tx_pending=false;
    tx_failed=false;
    error_due=false;
    if (timeout_ms>=UINT32_C(0x80000000)) Fault(CP_FAULT_TIME);
}
/* Only active shared requests and the latest GUI request are replayable. */
static bool Duplicate(const Cp_Command *command, Cp_Result *code)
{
    App_Command active;
    App_Snapshot snapshot=AppShared_ReadSnapshot();
    unsigned priority;
    for (priority=0U;priority<2U;++priority) {
        if (AppShared_ReadCommand(priority!=0U,&active) &&
            active.command.sequence==command->sequence) {
            *code=CP_SEQUENCE_CONFLICT;
            if (memcmp(active.command.identity,command->identity,CP_DLC)==0) {
                *code=CP_PENDING;
                if (active.state!=APP_PENDING)
                    *code=snapshot.result.request_id==active.request_id ?
                          snapshot.result.code : CP_SAFETY_INTERLOCK;
            }
            return true;
        }
    }
    if (!last.valid || last.command.sequence!=command->sequence) return false;
    *code=memcmp(last.command.identity,command->identity,CP_DLC)==0 ?
          last.code : CP_SEQUENCE_CONFLICT;
    return true;
}
static Cp_Result Execute(const Cp_Command *command, uint32_t now, bool valid, uint32_t *id)
{
    App_MotorStatus motor=AppShared_ReadSnapshot().motor;
    *id=0U;
    switch (command->opcode) {
    case CP_STOP:
        return AppShared_RequestStop(command,id);
    case CP_GET_STATUS:
        return CP_OK;
    case CP_KEEPALIVE:
        if (valid && timeout_ms!=0U && timeout_ms<UINT32_C(0x80000000)) {
            last_keepalive=now;
            lease_seen=true;
        }
        return CP_OK;
    case CP_SET_MAX_DUTY:
        if (g_CanAppDiagnostics.fault!=CP_FAULT_NONE) return CP_CAN_ERROR;
        max_duty=command->argument;
        limit_valid=true; /* CAN admission policy, never applied PWM evidence. */
        return CP_OK;
    default:
        if (!motor.valid || !motor.backend_ready) return CP_NOT_CONFIGURED;
        if (g_CanAppDiagnostics.fault!=CP_FAULT_NONE) return CP_CAN_ERROR;
        if (command->opcode!=CP_CLEAR_FAULT && !LeaseFresh(now,valid))
            return CP_SAFETY_INTERLOCK;
        if (command->opcode==CP_SET_DUTY && limit_valid && command->argument>max_duty)
            return CP_INVALID_ARGUMENT;
        return AppShared_PublishCommand(command,id);
    }
}
static void Receive(const Can_Frame *frame, uint32_t now, bool valid)
{
    Cp_DecodeResult decoded=CanProtocol_Decode(frame->id,false,false,frame->dlc,frame->data);
    Cp_Command *command=&decoded.command;
    Cp_Result code;
    uint8_t delta;
    if (decoded.valid && command->opcode==CP_STOP)
        AppShared_LatchInhibit(CP_FAULT_NONE); /* Never waits for a reply slot. */
    if (!decoded.valid) {
        ++g_CanAppDiagnostics.malformed;
        if (decoded.respond && !SetReply(command,decoded.result))
            ++g_CanAppDiagnostics.rx_dropped;
        return;
    }
    if (!ReplyFree(command->opcode==CP_STOP ? &stop_reply : &ordinary_reply)) {
        ++g_CanAppDiagnostics.rx_dropped; /* GUI retries; no command accepted. */
        return;
    }
    if (Duplicate(command,&code)) {
        (void)SetReply(command,code);
        return;
    }
    delta=(uint8_t)(command->sequence-last.command.sequence);
    if (last.valid && (delta==0U || delta>=128U)) {
        (void)SetReply(command,CP_STALE_SEQUENCE);
        return;
    }
    code=Execute(command,now,valid,&last.request_id);
    last.command=*command;
    last.code=code;
    last.valid=true;
    (void)SetReply(command,code);
}
static void Results(void)
{
    App_Command command;
    App_Result result;
    if (!AppShared_ReadResult(&command,&result)) return;
    if (!SetReply(&command.command,result.code)) return;
    if (last.valid && last.request_id==result.request_id) last.code=result.code;
    /* The reply slot now owns this result until transmitted. */
    (void)AppShared_AcknowledgeResult(result.request_id);
}
static void EncodeSnapshot(bool error, uint32_t now, bool valid)
{
    App_MotorStatus motor=AppShared_ReadSnapshot().motor;
    uint8_t flags=0U;
    uint16_t duty=0U;
    Cp_State state=CP_UNCONFIGURED;
    if (motor.valid) {
        state=motor.backend_ready ? motor.state : CP_UNCONFIGURED;
        if (motor.backend_ready) flags|=CP_FLAG_CONFIGURED;
        if (motor.safe_off_confirmed) flags|=CP_FLAG_SAFE_OFF;
        if (motor.reverse_supported) flags|=CP_FLAG_REVERSE_SUPPORTED;
        if (motor.reverse) flags|=CP_FLAG_REVERSE;
        if (motor.requested_duty_valid) duty=motor.requested_duty;
    }
    if (AppShared_HasPending()) flags|=CP_FLAG_OPERATION_PENDING;
    if (LeaseFresh(now,valid)) flags|=CP_FLAG_LEASE_FRESH;
    if (error) CanProtocol_EncodeError(inflight.data,flags,duty,g_CanAppDiagnostics.fault);
    else CanProtocol_EncodeStatus(inflight.data,state,flags,duty,g_CanAppDiagnostics.fault);
}
static void Transmit(uint32_t now, bool valid, bool can_send)
{
    Can_Status status;
    bool *sent_flag;
    if (tx_failed) return; /* Ownership after an API failure needs explicit recovery. */
    if (tx_pending) {
        if (CanDriver_GetState()!=CAN_DRIVER_RUNNING) return;
        status=CanDriver_PollTx();
        g_CanAppDiagnostics.last_api_status=status;
        if (status==CAN_STATUS_BUSY && CanDriver_GetState()==CAN_DRIVER_RUNNING) return;
        if (status!=CAN_STATUS_OK) { tx_failed=true; Fault(CP_FAULT_CAN_API); return; }
        tx_pending=false;
    }
    if (!can_send || CanDriver_GetState()!=CAN_DRIVER_RUNNING) return;
    /* Inflight cannot be changed until PollTx has consumed completion. */
    inflight.id=CP_RESPONSE_ID;
    inflight.dlc=CP_DLC;
    if (stop_reply.ack_due) {
        CanProtocol_EncodeAck(inflight.data,&stop_reply.command,stop_reply.code,g_CanAppDiagnostics.fault);
        sent_flag=&stop_reply.ack_due;
    } else if (error_due) {
        EncodeSnapshot(true,now,valid);
        sent_flag=&error_due;
    } else if (ordinary_reply.ack_due) {
        CanProtocol_EncodeAck(inflight.data,&ordinary_reply.command,ordinary_reply.code,g_CanAppDiagnostics.fault);
        sent_flag=&ordinary_reply.ack_due;
    } else if (ordinary_reply.status_due) {
        EncodeSnapshot(false,now,valid);
        sent_flag=&ordinary_reply.status_due;
    } else return;
    status=CanDriver_Transmit(&inflight);
    g_CanAppDiagnostics.last_api_status=status;
    if (status==CAN_STATUS_BUSY && CanDriver_GetState()==CAN_DRIVER_RUNNING) return;
    if (status!=CAN_STATUS_OK) { tx_failed=true; Fault(CP_FAULT_CAN_API); return; }
    *sent_flag=false;
    tx_pending=true;
}
void CanApp_Service(uint32_t now, bool valid)
{
    Can_Diagnostics diagnostics={0};
    Can_Frame received;
    Can_Status status;
    bool can_send=true;
    App_Inhibit safety=AppShared_ReadInhibit();
    if (safety.fault!=CP_FAULT_NONE && g_CanAppDiagnostics.fault==CP_FAULT_NONE)
        Fault(safety.fault);
    /* Checked before RX: a late KEEPALIVE cannot revive an expired lease. */
    if (lease_seen && !LeaseFresh(now,valid))
        Fault(valid ? CP_FAULT_COMMUNICATION : CP_FAULT_TIME);
    if (CanDriver_GetState()==CAN_DRIVER_RUNNING) {
        status=CanDriver_PollDiagnostics();
        g_CanAppDiagnostics.last_api_status=status;
        if (status!=CAN_STATUS_OK) { Fault(CP_FAULT_CAN_API); can_send=false; }
    }
    if (CanDriver_GetDiagnostics(&diagnostics)!=CAN_STATUS_OK) {
        Fault(CP_FAULT_CAN_API);
        can_send=false;
    }
    if (diagnostics.bus_off_seen) { Fault(CP_FAULT_BUS_OFF); can_send=false; }
    if (CanDriver_GetState()!=CAN_DRIVER_RUNNING) { Fault(CP_FAULT_CAN_API); can_send=false; }
    /* RX and safety continue even if the previous TX is still BUSY. */
    if (CanDriver_GetState()==CAN_DRIVER_RUNNING) {
        status=CanDriver_PollRx(&received);
        g_CanAppDiagnostics.last_api_status=status;
        if (status==CAN_STATUS_RX_OVERRUN) {
            ++g_CanAppDiagnostics.rx_overruns;
            Fault(CP_FAULT_RX_OVERRUN);
        }
        if (status==CAN_STATUS_OK || status==CAN_STATUS_RX_OVERRUN)
            Receive(&received,now,valid);
        else if (status==CAN_STATUS_INVALID_FRAME) {
            ++g_CanAppDiagnostics.malformed;
            Fault(CP_FAULT_RX_INVALID);
        } else if (status!=CAN_STATUS_NO_DATA) Fault(CP_FAULT_CAN_API);
    }
    Results();
    Transmit(now,valid,can_send);
    g_CanAppDiagnostics.tx_pending=tx_pending;
    g_CanAppDiagnostics.lease_fresh=LeaseFresh(now,valid);
}

