#include "app_shared.h"
#include <stddef.h>
#include <string.h>

typedef struct { App_Command request; bool taken; } Slot;
static Slot ordinary, stop;
static App_Snapshot snapshot;
static App_Inhibit inhibit;
static uint32_t next_id;

static Slot *Find(uint32_t id)
{
    if (id!=0U && ordinary.request.request_id==id) return &ordinary;
    if (id!=0U && stop.request.request_id==id) return &stop;
    return NULL;
}
static void StoreResult(Slot *slot, App_State state, Cp_Result code)
{
    slot->request.state=state;
    snapshot.result.request_id=slot->request.request_id;
    snapshot.result.state=state;
    snapshot.result.code=code;
}
void AppShared_Init(void)
{
    memset(&ordinary,0,sizeof ordinary);
    memset(&stop,0,sizeof stop);
    memset(&snapshot,0,sizeof snapshot);
    memset(&inhibit,0,sizeof inhibit);
    snapshot.motor.state=CP_UNCONFIGURED;
    next_id=1U;
}
static Cp_Result Publish(Slot *slot, const Cp_Command *command, uint32_t *id)
{
    if (slot->request.state!=APP_IDLE) return CP_BUSY;
    if (next_id==0U) return CP_SAFETY_INTERLOCK; /* Exhaustion, no ID reuse. */
    slot->request.request_id=next_id++;
    slot->request.command=*command;
    slot->request.state=APP_PENDING;
    slot->taken=false;
    *id=slot->request.request_id;
    return CP_PENDING;
}
Cp_Result AppShared_PublishCommand(const Cp_Command *command, uint32_t *id)
{
    const App_MotorStatus *motor=&snapshot.motor;
    if (id!=NULL) *id=0U;
    if (command==NULL || id==NULL) return CP_INVALID_ARGUMENT;
    if (command->opcode!=CP_START_FORWARD && command->opcode!=CP_START_REVERSE &&
        command->opcode!=CP_SET_DUTY && command->opcode!=CP_CLEAR_FAULT)
        return CP_INVALID_COMMAND;
    if ((command->opcode==CP_SET_DUTY && command->argument>1000U) ||
        (command->opcode!=CP_SET_DUTY && command->argument!=0U))
        return CP_INVALID_ARGUMENT;
    if (!motor->valid || !motor->backend_ready) return CP_NOT_CONFIGURED;
    if (ordinary.request.state!=APP_IDLE) return CP_BUSY;
    if (inhibit.latched) return CP_SAFETY_INTERLOCK;
    if (motor->fault!=CP_FAULT_NONE) return CP_MOTOR_FAULT;
    if (command->opcode==CP_START_REVERSE && !motor->reverse_supported)
        return CP_INVALID_STATE;
    return Publish(&ordinary,command,id);
}
void AppShared_LatchInhibit(Cp_Fault fault)
{
    inhibit.latched=true;
    if (inhibit.fault==CP_FAULT_NONE) inhibit.fault=fault;
    if (ordinary.request.state==APP_PENDING) {
        /* Revoke permission immediately, even if the result snapshot is busy. */
        ordinary.request.state=APP_ERROR;
        if (snapshot.result.state==APP_IDLE)
            StoreResult(&ordinary,APP_ERROR,CP_SAFETY_INTERLOCK);
    }
}
Cp_Result AppShared_RequestStop(const Cp_Command *command, uint32_t *id)
{
    if (id!=NULL) *id=0U;
    if (command==NULL || id==NULL || command->opcode!=CP_STOP || command->argument!=0U)
        return CP_INVALID_ARGUMENT;
    AppShared_LatchInhibit(CP_FAULT_NONE);
    return Publish(&stop,command,id);
}
App_Inhibit AppShared_ReadInhibit(void) { return inhibit; }
bool AppShared_ReadCommand(bool priority, App_Command *out)
{
    const Slot *slot=priority ? &stop : &ordinary;
    if (out==NULL || slot->request.state==APP_IDLE) return false;
    *out=slot->request;
    return true;
}
bool AppShared_TakeCommand(App_Command *out)
{
    Slot *slot;
    if (out==NULL) return false;
    if (stop.request.state==APP_PENDING && !stop.taken) slot=&stop;
    else if (ordinary.request.state==APP_PENDING && !ordinary.taken && !inhibit.latched)
        slot=&ordinary;
    else return false;
    slot->taken=true;
    *out=slot->request;
    return true;
}
bool AppShared_IsCurrent(uint32_t id)
{
    const Slot *slot=Find(id);
    return slot!=NULL && slot->taken && slot->request.state==APP_PENDING &&
           (slot==&stop || !inhibit.latched);
}
static bool CompletionEvidence(const App_Command *request)
{
    const App_MotorStatus *motor=&snapshot.motor;
    if (!motor->valid || !motor->backend_ready) return false;
    switch (request->command.opcode) {
    case CP_STOP:
        return motor->safe_off_confirmed && motor->state==CP_STOPPED;
    case CP_START_FORWARD:
    case CP_START_REVERSE:
        return motor->state==CP_RUNNING && motor->fault==CP_FAULT_NONE &&
               motor->reverse==(request->command.opcode==CP_START_REVERSE);
    case CP_SET_DUTY:
        return motor->applied_duty_valid && motor->fault==CP_FAULT_NONE &&
               motor->applied_duty==request->command.argument;
    case CP_CLEAR_FAULT:
        return motor->fault==CP_FAULT_NONE && motor->safe_off_confirmed;
    default:
        return false;
    }
}
bool AppShared_PublishResult(uint32_t id, App_State state, Cp_Result code)
{
    Slot *slot=Find(id);
    if (!AppShared_IsCurrent(id) || snapshot.result.state!=APP_IDLE) return false;
    if (state==APP_DONE) {
        if (code!=CP_COMPLETED || !CompletionEvidence(&slot->request)) return false;
    } else if (state!=APP_ERROR || (unsigned)code<0x80U || (unsigned)code>0x8AU)
        return false;
    StoreResult(slot,state,code);
    if (code==CP_MOTOR_FAULT) AppShared_LatchInhibit(CP_FAULT_BACKEND);
    if (code==CP_INTERNAL_TIMEOUT) AppShared_LatchInhibit(CP_FAULT_BACKEND_TIMEOUT);
    return true;
}
bool AppShared_PublishStatus(const App_MotorStatus *motor)
{
    if (motor==NULL || (unsigned)motor->state>(unsigned)CP_FAULT ||
        (unsigned)motor->fault>(unsigned)CP_FAULT_BACKEND_TIMEOUT ||
        motor->requested_duty>1000U || motor->applied_duty>1000U ||
        (motor->backend_ready && (!motor->valid || motor->state==CP_UNCONFIGURED)) ||
        (motor->safe_off_confirmed && (!motor->valid || motor->state!=CP_STOPPED)) ||
        ((motor->state==CP_RUNNING || motor->state==CP_STARTING) &&
         (!motor->valid || !motor->backend_ready || motor->safe_off_confirmed)))
        return false;
    snapshot.motor=*motor;
    if (motor->fault!=CP_FAULT_NONE) AppShared_LatchInhibit(motor->fault);
    if (!motor->backend_ready && ordinary.request.state==APP_PENDING)
        AppShared_LatchInhibit(CP_FAULT_BACKEND);
    return true;
}
App_Snapshot AppShared_ReadSnapshot(void) { return snapshot; }
bool AppShared_ReadResult(App_Command *command, App_Result *result)
{
    const Slot *slot=Find(snapshot.result.request_id);
    if (command==NULL || result==NULL || slot==NULL || snapshot.result.state==APP_IDLE)
        return false;
    *command=slot->request;
    *result=snapshot.result;
    return true;
}
bool AppShared_AcknowledgeResult(uint32_t id)
{
    Slot *slot=Find(id);
    if (slot==NULL || snapshot.result.state==APP_IDLE || snapshot.result.request_id!=id)
        return false;
    memset(slot,0,sizeof *slot);
    memset(&snapshot.result,0,sizeof snapshot.result);
    /* A cancellation can wait behind one already-published terminal result. */
    if (ordinary.request.state==APP_ERROR)
        StoreResult(&ordinary,APP_ERROR,CP_SAFETY_INTERLOCK);
    return true;
}
bool AppShared_HasPending(void)
{ return ordinary.request.state==APP_PENDING || stop.request.state==APP_PENDING; }
bool AppShared_ReleaseInhibit(void)
{
    const App_MotorStatus *motor=&snapshot.motor;
    if (!inhibit.latched || inhibit.fault!=CP_FAULT_NONE ||
        ordinary.request.state!=APP_IDLE || stop.request.state!=APP_IDLE ||
        snapshot.result.state!=APP_IDLE || !motor->valid || !motor->backend_ready ||
        !motor->safe_off_confirmed || motor->state!=CP_STOPPED || motor->fault!=CP_FAULT_NONE)
        return false;
    inhibit.latched=false;
    return true;
}
