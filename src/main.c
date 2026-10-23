/** CAN command -> shared mailbox -> GPIO/HB2000 SPI -> CAN feedback.
 * Existing drivers and generated settings are reused without edits.
 * New application modules are under src/application and src/protocol.
 */
#include "Clock_Ip.h"
#include "Port_Ci_Port_Ip.h"
#include "Port_Ci_Port_Ip_Cfg.h"
#include "IntCtrl_Ip.h"
#include "IntCtrl_Ip_Cfg.h"
#include "OsIf.h"
#include "application/app_time.h"
#include "application/hb2000_io.h"
#include "application/can_app.h"
#include "application/spi_motor_app.h"

/* Stage: 1 clocks/safe GPIO, 2 generated pins, 3 interrupts,
 * 4 real ms timer, 5 application/driver init, 6 foreground services.
 * HB initialization continues in SpiMotorApp_Service; inspect
 * g_SpiMotorStatus.backend_ready and g_SpiMotorPhase for its progress. */
volatile uint32 g_InitStage;
volatile Clock_Ip_StatusType g_ClockStatus;
volatile Port_Ci_Port_Ip_PortStatusType g_PortStatus;
volatile IntCtrl_Ip_StatusType g_IntCtrlStatus;
volatile Can_Status g_CanInitStatus=CAN_STATUS_BAD_STATE;
volatile Can_Status g_CanStartStatus=CAN_STATUS_BAD_STATE;
volatile bool g_CanReady;
volatile uint32_t g_ControlCycles;

int main(void)
{
    const Can_Config can_config={CP_COMMAND_ID,CAN_MODE_NORMAL};
    const CanApp_Config application_config={1000U};
    uint32_t core_hz;

    AppShared_Init(); /* Boot only. Never use this to clear a live inhibit. */
    g_InitStage=1U;
    g_ClockStatus=Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
    HbIo_InitPins();
    if (g_ClockStatus!=CLOCK_IP_SUCCESS) {
        SpiMotorApp_SystemFault(3U);
        for (;;) { }
    }
    core_hz=Clock_Ip_GetClockFrequency(CORE_CLK);
    OsIf_Init(NULL_PTR);
    g_InitStage=2U;
    g_PortStatus=Port_Ci_Port_Ip_Init(NUM_OF_CONFIGURED_PINS0,g_pin_mux_InitConfigArr0);
    if (g_PortStatus!=PORT_CI_PORT_SUCCESS) {
        SpiMotorApp_SystemFault(6U);
        for (;;) { }
    }
    g_InitStage=3U;
    g_IntCtrlStatus=IntCtrl_Ip_Init(&IntCtrlConfig_0);
    if (g_IntCtrlStatus!=INTCTRL_IP_STATUS_SUCCESS) {
        SpiMotorApp_SystemFault(7U);
        for (;;) { }
    }
    g_InitStage=4U;
    if (!AppTime_Init(core_hz)) {
        SpiMotorApp_SystemFault(5U);
        for (;;) { }
    }
    g_InitStage=5U;
    CanApp_Init(&application_config);
    g_CanInitStatus=CanDriver_Init(&can_config);
    if (g_CanInitStatus==CAN_STATUS_OK)
        g_CanStartStatus=CanDriver_Start();
    SpiMotorApp_Init(AppTime_NowMs());

    g_InitStage=6U;
    for (;;) {
        uint32_t now=AppTime_NowMs();
        bool time_valid=AppTime_IsValid();
        /* CAN revokes permission/queues STOP before each hardware round. */
        CanApp_Service(now,time_valid);
        now=AppTime_NowMs();
        time_valid=AppTime_IsValid();
        SpiMotorApp_Service(now,time_valid,CanApp_LocalStartAllowed(now,time_valid));
        g_CanReady=CanDriver_GetState()==CAN_DRIVER_RUNNING;
        ++g_ControlCycles;
        /* No 5 ms sleep: the single CAN RX mailbox needs frequent service. */
    }
}
