#ifndef APP_SHARED_H
#define APP_SHARED_H
#include <stdbool.h>
#include <stdint.h>
#include "../protocol/can_protocol.h"

/* One foreground caller only. No ISR/DMA callback/RTOS access. */
typedef enum { APP_IDLE=0, APP_PENDING, APP_DONE, APP_ERROR } App_State;
typedef struct {
    uint32_t request_id;
    Cp_Command command;
    App_State state;
} App_Command;
typedef struct {
    uint32_t request_id;
    App_State state;
    Cp_Result code;
} App_Result;
typedef struct {
    bool valid, backend_ready;
    bool requested_duty_valid, applied_duty_valid, safe_off_confirmed;
    bool reverse_supported, reverse;
    Cp_State state;
    uint16_t requested_duty, applied_duty;
    Cp_Fault fault;
} App_MotorStatus;
typedef struct {
    App_MotorStatus motor;
    App_Result result; /* One terminal result, retained until acknowledged. */
} App_Snapshot;
typedef struct { bool latched; Cp_Fault fault; } App_Inhibit;

void AppShared_Init(void); /* Boot only, never live fault recovery. */
/* CAN publishes; OK-to-queue returns CP_PENDING, never CP_COMPLETED. */
Cp_Result AppShared_PublishCommand(const Cp_Command *command, uint32_t *id);
Cp_Result AppShared_RequestStop(const Cp_Command *command, uint32_t *id);
void AppShared_LatchInhibit(Cp_Fault fault); /* Immediate; independent of CAN TX. */
App_Inhibit AppShared_ReadInhibit(void);
bool AppShared_ReadCommand(bool priority, App_Command *out);
/* SPI takes once, STOP first; recheck IsCurrent before each physical step. */
bool AppShared_TakeCommand(App_Command *out);
bool AppShared_IsCurrent(uint32_t id);
/* SPI publishes real evidence before DONE/COMPLETED. False means not stored;
 * retry after CAN consumes the previous result; never wait in a loop. */
bool AppShared_PublishStatus(const App_MotorStatus *status);
bool AppShared_PublishResult(uint32_t id, App_State state, Cp_Result code);
App_Snapshot AppShared_ReadSnapshot(void);
/* CAN copies a terminal result into a retained reply before acknowledging it. */
bool AppShared_ReadResult(App_Command *command, App_Result *result);
bool AppShared_AcknowledgeResult(uint32_t id);
bool AppShared_HasPending(void);
/* Explicit foreground recovery of STOP-only inhibit after confirmed safe-off.
 * Fault inhibits require coordinated safe shutdown/reinitialization. */
bool AppShared_ReleaseInhibit(void);
#endif
