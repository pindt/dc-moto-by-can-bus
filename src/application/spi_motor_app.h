#ifndef SPI_MOTOR_APP_H
#define SPI_MOTOR_APP_H
#include "app_shared.h"

extern volatile App_MotorStatus g_SpiMotorStatus;
extern volatile uint32_t g_SpiMotorPhase;
extern volatile uint32_t g_Fault;
extern volatile bool g_MotorOn;
extern volatile uint16_t g_HbId, g_HbStatus, g_HbConfig, g_HbConfigExpected;
extern volatile uint8_t g_StartButton, g_StopButton;

void SpiMotorApp_Init(uint32_t now_ms);
/* One bounded foreground round. Commands remain active without CAN traffic. */
void SpiMotorApp_Service(uint32_t now_ms, bool time_valid, bool local_start_allowed);
void SpiMotorApp_SystemFault(uint32_t reason);
#endif
