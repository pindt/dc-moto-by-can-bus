#ifndef CAN_APP_H
#define CAN_APP_H
#include <stdbool.h>
#include <stdint.h>
#include "app_shared.h"
#include "../drivers_can/can/inc/can_driver.h"

typedef struct {
    uint32_t communication_timeout_ms; /* 0 = CAN-only, physical START disabled. */
} CanApp_Config;
typedef struct {
    uint32_t malformed, rx_overruns, rx_dropped;
    Can_Status last_api_status;
    Cp_Fault fault;
    bool tx_pending, lease_fresh;
} CanApp_Diagnostics;
extern volatile CanApp_Diagnostics g_CanAppDiagnostics;
/* Sole foreground CAN owner; initialize once, with no outstanding transfers. */
void CanApp_Init(const CanApp_Config *config);
/* One bounded round, no waits. now_ms must be real monotonic milliseconds.
 * time_valid=false disables physical START/duty and revokes an active lease. */
void CanApp_Service(uint32_t now_ms, bool time_valid);
#endif
