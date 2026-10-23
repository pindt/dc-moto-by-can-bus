#ifndef CAN_APP_H
#define CAN_APP_H
#include <stdbool.h>
#include <stdint.h>
#include "app_shared.h"
#include "can_driver.h"

typedef struct {
    uint32_t communication_timeout_ms; /* Legacy main.c argument; ignored in V2. */
} CanApp_Config;
typedef struct {
    uint32_t malformed, rx_overruns, rx_dropped;
    uint32_t service_cycles;
    Can_Status last_api_status;
    Cp_Fault fault;
    bool tx_pending, tx_failed, control_allowed;
} CanApp_Diagnostics;
extern volatile CanApp_Diagnostics g_CanAppDiagnostics;
/* Cached driver details for the debugger, including bus-off/error counters.
 * This does not add fields to any CAN frame. */
extern volatile Can_Diagnostics g_CanDriverDiagnostics;
/* Sole foreground CAN owner; initialize once, with no outstanding transfers. */
void CanApp_Init(const CanApp_Config *config);
/* One bounded foreground round. No KEEPALIVE, sequence or client ACK. */
void CanApp_Service(uint32_t now_ms, bool time_valid);
/* Physical START uses the same time/fault checks, without a CAN heartbeat. */
bool CanApp_LocalStartAllowed(uint32_t now_ms, bool time_valid);
#endif
