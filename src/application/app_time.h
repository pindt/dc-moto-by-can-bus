#ifndef APP_TIME_H
#define APP_TIME_H
#include <stdbool.h>
#include <stdint.h>

extern volatile uint32_t g_CoreHz;
extern volatile uint32_t g_AppNowMs;
bool AppTime_Init(uint32_t core_hz);
uint32_t AppTime_NowMs(void);
bool AppTime_IsValid(void);
/* Short foreground hardware settling delay; never resets the ms clock. */
bool AppTime_DelayUs(uint32_t microseconds);
void SysTick_Handler(void);
#endif
