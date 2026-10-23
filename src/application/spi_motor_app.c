#include "spi_motor_app.h"
#include "hb2000_io.h"
#include <string.h>

#define HB_READ_ID UINT16_C(0x0000)
#define HB_READ_STATUS UINT16_C(0x2000)
#define HB_READ_CONFIG UINT16_C(0x6000)
#define HB_WRITE_CONFIG UINT16_C(0xE000)
#define HB_CLEAR_FAULTS UINT16_C(0xAFFF)
#define HB_CONFIG_EN UINT16_C(0x0010)
#define HB_CONFIG_MASK UINT16_C(0x1FFF)
/* OL, OC and TW remain visible diagnostics; hardware current limiting remains on.
 * CP_U, UV, OV, shorts, OT and malformed SPI still stop the motor. */
#define HB_BLOCKING_MASK UINT16_C(0x0FF1)
#define REVERSE_OFF_MS UINT32_C(100)

typedef enum {
    PH_BOOT_WAIT=1, PH_BOOT_ID, PH_BOOT_CONFIG,
    PH_OFF_WRITE, PH_OFF_VERIFY, PH_OFF_WAIT, PH_OFF_CLEAR, PH_OFF_STATUS,
    PH_IDLE, PH_ON_WRITE, PH_ON_VERIFY, PH_ON_WAIT, PH_ON_STATUS,
    PH_ON_CONFIRM, PH_FAULT_IDLE, PH_DIRECTION_WAIT
} Phase;
volatile App_MotorStatus g_SpiMotorStatus;
volatile uint32_t g_SpiMotorPhase, g_Fault;
volatile bool g_MotorOn;
volatile uint16_t g_HbId, g_HbStatus, g_HbConfig, g_HbConfigExpected;
volatile uint8_t g_StartButton, g_StopButton;

static App_MotorStatus s_motor;
static App_Command s_request;
static Phase s_phase;
static uint32_t s_deadline, s_last_status, s_start_change, s_stop_change, s_reverse_wait;
static bool s_boot_complete, s_config_known, s_disabled, s_hb_valid;
static bool s_active, s_local, s_outcome_pending, s_recovering, s_resume, s_direction;
static bool s_start_raw, s_start_stable, s_stop_held;
static bool s_reverse_delay;
static Cp_Result s_outcome;
static uint8_t s_off_attempts;

static bool Due(uint32_t now, uint32_t deadline)
{ return (uint32_t)(now-deadline)<UINT32_C(0x80000000); }
static bool OffPhase(void)
{ return s_phase>=PH_OFF_WRITE && s_phase<=PH_OFF_STATUS; }
static void Publish(void)
{
    s_motor.hb_valid=s_hb_valid;
    s_motor.hb_id=g_HbId;
    s_motor.hb_status=g_HbStatus;
    s_motor.hb_config=g_HbConfig;
    g_SpiMotorStatus=s_motor;
    g_SpiMotorPhase=(uint32_t)s_phase;
    (void)AppShared_PublishStatus(&s_motor);
}
static void PinsOff(void)
{
    HbIo_SetRun(false);
    g_MotorOn=false;
    s_motor.applied_duty=0U;
    s_motor.applied_duty_valid=false;
}
static void MarkFault(uint32_t reason, Cp_Fault fault)
{
    PinsOff();
    if (g_Fault==0U) g_Fault=reason;
    if (s_motor.fault==CP_FAULT_NONE) s_motor.fault=fault;
    s_motor.valid=true;
    s_motor.state=CP_FAULT;
    s_motor.safe_off_confirmed=false;
    AppShared_LatchInhibit(fault);
}
static void TryResult(void)
{
    if (!s_active || !s_outcome_pending) return;
    if (s_outcome==CP_COMPLETED && s_motor.fault!=CP_FAULT_NONE)
        s_outcome=CP_MOTOR_FAULT;
    if (s_local || !AppShared_IsCurrent(s_request.request_id) ||
        AppShared_PublishResult(s_request.request_id,
            s_outcome==CP_COMPLETED ? APP_DONE : APP_ERROR,s_outcome)) {
        s_active=false;
        s_local=false;
        s_outcome_pending=false;
    }
}
static void Finish(Cp_Result result)
{
    s_outcome=result;
    s_outcome_pending=true;
    Publish();
    TryResult();
}
static void BeginOff(bool resume)
{
    PinsOff();
    s_resume=resume;
    if (s_config_known && s_off_attempts<3U) {
        if (!OffPhase()) s_phase=PH_OFF_WRITE;
    } else s_phase=PH_FAULT_IDLE;
    s_motor.state=s_motor.fault==CP_FAULT_NONE ? CP_STOPPING : CP_FAULT;
    s_motor.safe_off_confirmed=false;
}
static void Fail(uint32_t reason)
{
    MarkFault(reason,reason==5U ? CP_FAULT_TIME : CP_FAULT_BACKEND);
    s_recovering=false;
    BeginOff(false);
    if (s_disabled) s_phase=PH_FAULT_IDLE;
    if (s_active && (s_local || AppShared_IsCurrent(s_request.request_id)))
        Finish(CP_MOTOR_FAULT);
}
static bool ReadRegister(uint16_t command, volatile uint16_t *out)
{
    uint16_t value;
    HbIo_Result result=HbIo_Read(command,&value);
    if (result!=HB_IO_OK) {
        s_hb_valid=false;
        Fail(result==HB_IO_PROTOCOL_ERROR ? 2U : (result==HB_IO_TIME_ERROR ? 5U : 1U));
        return false;
    }
    *out=value;
    return true;
}
static bool WriteConfig(uint16_t payload)
{
    uint16_t ignored;
    HbIo_Result result=HbIo_Transfer((uint16_t)(HB_WRITE_CONFIG|payload),&ignored);
    if (result!=HB_IO_OK) {
        s_hb_valid=false;
        Fail(result==HB_IO_TIME_ERROR ? 5U : 1U);
        return false;
    }
    return true;
}
static bool VerifyConfig(uint16_t expected)
{
    if (!ReadRegister(HB_READ_CONFIG,&g_HbConfig)) return false;
    if ((g_HbConfig&HB_CONFIG_MASK)!=expected) { Fail(2U); return false; }
    return true;
}
static bool ReadStatus(uint32_t now)
{
    if (!ReadRegister(HB_READ_STATUS,&g_HbStatus)) return false;
    s_hb_valid=true;
    s_last_status=now;
    if ((g_HbStatus&HB_BLOCKING_MASK)!=0U) { Fail(4U); return false; }
    return true;
}
static void Stopped(void)
{
    PinsOff();
    if (!HbIo_OutputMatches(s_motor.reverse,0U)) { Fail(2U); return; }
    s_motor.valid=true;
    s_motor.backend_ready=s_boot_complete;
    s_motor.state=s_motor.fault==CP_FAULT_NONE ? CP_STOPPED : CP_FAULT;
    s_motor.safe_off_confirmed=s_disabled && s_motor.fault==CP_FAULT_NONE;
    s_motor.applied_duty_valid=s_motor.fault==CP_FAULT_NONE;
    s_phase=s_motor.fault==CP_FAULT_NONE ? PH_IDLE : PH_FAULT_IDLE;
}
static bool PermitRun(bool allowed)
{
    return allowed && s_active && !s_stop_held && s_motor.fault==CP_FAULT_NONE &&
           !AppShared_ReadInhibit().latched &&
           (s_local || AppShared_IsCurrent(s_request.request_id));
}
static void Accept(const App_Command *request, uint32_t now, bool local)
{
    s_request=*request;
    s_active=true;
    s_local=local;
    s_outcome_pending=false;
    s_off_attempts=0U;
    if (request->command.opcode==CP_STOP) {
        s_recovering=false;
        s_resume=false;
        PinsOff();
        if (s_config_known) BeginOff(false);
        else if (s_phase==PH_FAULT_IDLE) Finish(CP_NOT_CONFIGURED);
        /* An early STOP leaves the safe boot sequence running, then completes. */
        return;
    }
    if (s_stop_held) { Finish(CP_SAFETY_INTERLOCK); return; }
    if (request->command.opcode==CP_CLEAR_FAULT) {
        /* Reinitialize SPI and verify EN=0 plus freshly cleared HB status.
         * Never reset shared mailboxes or turn the motor on during recovery. */
        PinsOff();
        s_recovering=true;
        s_resume=false;
        s_boot_complete=false;
        s_motor.backend_ready=false;
        s_motor.safe_off_confirmed=false;
        s_config_known=false;
        s_disabled=false;
        HbIo_InitPins();
        s_motor.reverse=false;
        s_direction=false;
        if (!HbIo_InitSpi()) { Fail(1U); return; }
        s_deadline=now+10U;
        s_phase=PH_BOOT_WAIT;
        return;
    }
    if (!s_boot_complete) { Finish(CP_NOT_CONFIGURED); return; }
    if (s_motor.fault!=CP_FAULT_NONE) { Finish(CP_MOTOR_FAULT); return; }
    if (request->command.opcode==CP_SET_DUTY && request->command.argument!=0U &&
        request->command.argument!=1000U) { Finish(CP_INVALID_ARGUMENT); return; }
    if (request->command.opcode==CP_START_FORWARD) s_direction=false;
    if (request->command.opcode==CP_START_REVERSE) s_direction=true;
    if (request->command.opcode==CP_SET_DUTY) {
        s_motor.requested_duty=request->command.argument;
        if (request->command.argument==0U) { BeginOff(false); return; }
    } else s_motor.requested_duty=1000U;
    if (g_MotorOn && s_motor.reverse==s_direction) {
        if (!HbIo_SetOutput(s_direction,s_motor.requested_duty)) { Fail(2U); return; }
        s_motor.applied_duty=s_motor.requested_duty;
        s_motor.applied_duty_valid=true;
        s_motor.state=CP_RUNNING;
        Finish(CP_COMPLETED);
        return;
    }
    if (g_MotorOn && s_motor.reverse!=s_direction) {
        s_reverse_wait=now+REVERSE_OFF_MS;
        s_reverse_delay=true;
    } else if (s_motor.reverse==s_direction) s_reverse_delay=false;
    BeginOff(true);
}
static bool Buttons(uint32_t now)
{
    bool pressed=HbIo_StartPressed(), edge=false;
    g_StartButton=pressed ? 1U : 0U;
    g_StopButton=HbIo_StopPressed() ? 1U : 0U;
    if (pressed!=s_start_raw) { s_start_raw=pressed; s_start_change=now; }
    if (pressed!=s_start_stable && (uint32_t)(now-s_start_change)>=20U) {
        s_start_stable=pressed;
        edge=pressed;
    }
    if (g_StopButton!=0U) {
        s_stop_held=true;
        s_stop_change=now;
        PinsOff();
        AppShared_LatchInhibit(CP_FAULT_NONE);
    } else if (s_stop_held && (uint32_t)(now-s_stop_change)>=20U) s_stop_held=false;
    return edge && !s_stop_held;
}
void SpiMotorApp_Init(uint32_t now)
{
    memset(&s_motor,0,sizeof s_motor);
    memset(&s_request,0,sizeof s_request);
    g_Fault=0U; g_HbId=0U; g_HbStatus=0U; g_HbConfig=0U; g_HbConfigExpected=0U;
    s_boot_complete=false; s_config_known=false; s_disabled=false; s_hb_valid=false;
    s_active=false; s_local=false; s_outcome_pending=false; s_recovering=false;
    s_resume=false; s_direction=false; s_off_attempts=0U;
    s_start_raw=false; s_start_stable=false; s_stop_held=false;
    s_start_change=now; s_stop_change=now; s_last_status=now; s_reverse_wait=0U;
    s_reverse_delay=false;
    s_motor.state=CP_UNCONFIGURED;
    s_motor.requested_duty_valid=true;
    s_motor.requested_duty=1000U;
    s_motor.reverse_supported=true;
    HbIo_InitPins();
    PinsOff();
    s_phase=PH_BOOT_WAIT;
    s_deadline=now+10U;
    if (!HbIo_InitSpi()) { Fail(1U); s_phase=PH_FAULT_IDLE; }
    Publish();
}
void SpiMotorApp_SystemFault(uint32_t reason)
{
    MarkFault(reason,CP_FAULT_BACKEND);
    Publish();
}
void SpiMotorApp_Service(uint32_t now, bool time_valid, bool start_allowed)
{
    App_Inhibit inhibit;
    App_Command request, peek;
    uint16_t ignored;
    bool take, local_edge=Buttons(now);
    if (!time_valid) {
        PinsOff();
        if (s_boot_complete) MarkFault(5U,CP_FAULT_TIME);
        Publish();
        return;
    }
    if (s_active && !s_local && !AppShared_IsCurrent(s_request.request_id)) {
        s_active=false; s_outcome_pending=false; s_recovering=false; s_resume=false;
    }
    inhibit=AppShared_ReadInhibit();
    if (inhibit.latched && !s_recovering) {
        if (inhibit.fault!=CP_FAULT_NONE && s_motor.fault==CP_FAULT_NONE)
            MarkFault(8U,inhibit.fault);
        if (g_MotorOn || (!s_disabled && !OffPhase() && s_config_known))
            BeginOff(false);
        else PinsOff();
    }
    TryResult();
    take=AppShared_ReadCommand(true,&peek) && peek.state==APP_PENDING;
    if (!take && AppShared_ReadCommand(false,&peek) && peek.state==APP_PENDING)
        take=s_boot_complete || peek.command.opcode==CP_CLEAR_FAULT;
    if (take && (!s_active || s_local) && AppShared_TakeCommand(&request))
        Accept(&request,now,false);
    if (local_edge && start_allowed && s_phase==PH_IDLE && !s_active &&
        !AppShared_ReadInhibit().latched && s_motor.fault==CP_FAULT_NONE) {
        memset(&request,0,sizeof request);
        request.command.opcode=CP_START;
        Accept(&request,now,true);
    }
    switch (s_phase) {
    case PH_BOOT_WAIT:
        if (Due(now,s_deadline)) s_phase=PH_BOOT_ID;
        break;
    case PH_BOOT_ID:
        if (ReadRegister(HB_READ_ID,&g_HbId)) {
            if ((g_HbId&0xFFF0U)!=0x0010U) Fail(2U);
            else s_phase=PH_BOOT_CONFIG;
        }
        break;
    case PH_BOOT_CONFIG:
        if (ReadRegister(HB_READ_CONFIG,&g_HbConfig)) {
            g_HbConfigExpected=(uint16_t)((g_HbConfig&0x0FE0U)|0x0418U);
            s_config_known=true;
            s_phase=PH_OFF_WRITE;
        }
        break;
    case PH_OFF_WRITE:
        ++s_off_attempts;
        if (WriteConfig((uint16_t)(g_HbConfigExpected&~HB_CONFIG_EN))) s_phase=PH_OFF_VERIFY;
        break;
    case PH_OFF_VERIFY:
        if (VerifyConfig((uint16_t)(g_HbConfigExpected&~HB_CONFIG_EN))) {
            s_disabled=true;
            s_deadline=now+1U;
            s_phase=PH_OFF_WAIT;
        }
        break;
    case PH_OFF_WAIT:
        if (Due(now,s_deadline)) s_phase=s_boot_complete ? PH_OFF_STATUS : PH_OFF_CLEAR;
        break;
    case PH_OFF_CLEAR:
        if (HbIo_Transfer(HB_CLEAR_FAULTS,&ignored)!=HB_IO_OK) Fail(1U);
        else { s_deadline=now+1U; s_phase=PH_OFF_STATUS; }
        break;
    case PH_OFF_STATUS:
        if (!Due(now,s_deadline) || !ReadStatus(now)) break;
        s_boot_complete=true;
        if (s_recovering) {
            if (!s_active || !AppShared_IsCurrent(s_request.request_id) || s_stop_held) {
                s_recovering=false; Fail(8U); break;
            }
            s_motor.fault=CP_FAULT_NONE;
            g_Fault=0U;
            Stopped();
            Publish();
            if (!AppShared_FinishRecovery(s_request.request_id)) { Fail(8U); break; }
            s_recovering=false;
            Finish(CP_COMPLETED);
        } else if (s_resume && s_motor.fault==CP_FAULT_NONE) {
            s_motor.backend_ready=true;
            s_motor.state=CP_STARTING;
            s_deadline=s_reverse_delay ? s_reverse_wait : now;
            s_phase=PH_DIRECTION_WAIT;
        } else {
            Stopped();
            if (s_active) Finish(s_motor.fault==CP_FAULT_NONE ? CP_COMPLETED : CP_MOTOR_FAULT);
        }
        break;
    case PH_DIRECTION_WAIT:
        if (!PermitRun(start_allowed)) { BeginOff(false); break; }
        if (!Due(now,s_deadline)) break;
        if (!HbIo_SetOutput(s_direction,0U)) { Fail(2U); break; }
        s_motor.reverse=s_direction;
        s_reverse_delay=false;
        s_phase=PH_ON_WRITE;
        break;
    case PH_ON_WRITE:
        if (!PermitRun(start_allowed)) { BeginOff(false); break; }
        s_disabled=false;
        if (WriteConfig(g_HbConfigExpected)) s_phase=PH_ON_VERIFY;
        break;
    case PH_ON_VERIFY:
        if (!PermitRun(start_allowed)) { BeginOff(false); break; }
        if (VerifyConfig(g_HbConfigExpected)) { s_deadline=now+1U; s_phase=PH_ON_WAIT; }
        break;
    case PH_ON_WAIT:
        if (!PermitRun(start_allowed)) BeginOff(false);
        else if (Due(now,s_deadline)) s_phase=PH_ON_STATUS;
        break;
    case PH_ON_STATUS:
        if (!PermitRun(start_allowed)) { BeginOff(false); break; }
        if (!ReadStatus(now)) break;
        if (!HbIo_SetOutput(s_motor.reverse,s_motor.requested_duty)) { Fail(2U); break; }
        g_MotorOn=true;
        s_deadline=now+1U;
        s_phase=PH_ON_CONFIRM;
        break;
    case PH_ON_CONFIRM:
        if (!PermitRun(start_allowed)) { BeginOff(false); break; }
        if (!Due(now,s_deadline) || !ReadStatus(now)) break;
        if (!HbIo_OutputMatches(s_motor.reverse,s_motor.requested_duty)) { Fail(2U); break; }
        s_motor.valid=true; s_motor.backend_ready=true;
        s_motor.state=CP_RUNNING;
        s_motor.safe_off_confirmed=false;
        s_motor.applied_duty=s_motor.requested_duty;
        s_motor.applied_duty_valid=true;
        s_phase=PH_IDLE;
        Finish(CP_COMPLETED);
        break;
    case PH_IDLE:
        if ((uint32_t)(now-s_last_status)>=50U) {
            if (ReadStatus(now) &&
                !HbIo_OutputMatches(s_motor.reverse,g_MotorOn ? s_motor.applied_duty : 0U))
                Fail(2U);
        }
        break;
    case PH_FAULT_IDLE:
        PinsOff();
        if ((uint32_t)(now-s_last_status)>=50U) {
            uint16_t status;
            s_last_status=now;
            if (HbIo_Read(HB_READ_STATUS,&status)==HB_IO_OK) {
                g_HbStatus=status;
                s_hb_valid=true; /* Refresh cache without silently clearing the latch. */
            } else s_hb_valid=false;
        }
        break;
    default:
        Fail(2U);
        break;
    }
    Publish();
    TryResult();
    if (!s_stop_held && s_phase==PH_IDLE && s_motor.safe_off_confirmed)
        (void)AppShared_ReleaseInhibit();
}
