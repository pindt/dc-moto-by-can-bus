#include "app_time.h"
#include "OsIf_Cfg.h"
#include "S32K144_SYSTICK.h"

#if (OSIF_USE_SYSTEM_TIMER == STD_ON)
#error "Application SysTick ownership requires the current OSIF system timer disabled"
#endif

volatile uint32_t g_CoreHz;
volatile uint32_t g_AppNowMs;
static uint32_t s_period;
static volatile bool s_tick_seen;
static bool s_configured;

bool AppTime_Init(uint32_t core_hz)
{
    s_configured=false;
    s_tick_seen=false;
    g_CoreHz=core_hz;
    g_AppNowMs=0U;
    if (core_hz<1000000U || core_hz%1000U!=0U || core_hz/1000U>0x1000000U)
        return false;
    s_period=core_hz/1000U;
    S32_SysTick->CSRr=0U;
    S32_SysTick->RVR=s_period-1U;
    S32_SysTick->CVR=0U;
    s_configured=true;
    S32_SysTick->CSRr=7U; /* Core clock, 1 ms interrupt, enabled. */
    return true;
}
/* Overrides the existing weak startup handler. Only time is shared with ISR;
 * motor/CAN/SPI mailboxes are exclusively accessed by the foreground loop. */
void SysTick_Handler(void)
{
    ++g_AppNowMs;
    s_tick_seen=true;
}
uint32_t AppTime_NowMs(void) { return g_AppNowMs; }
bool AppTime_IsValid(void)
{
    uint32_t masked=0U;
#if defined(__arm__) || defined(__thumb__)
    __asm volatile ("mrs %0, primask" : "=r" (masked));
#endif
    return s_configured && s_tick_seen &&
           masked==0U &&
           (S32_SysTick->CSRr&7U)==7U && S32_SysTick->RVR==s_period-1U;
}
bool AppTime_DelayUs(uint32_t microseconds)
{
    uint32_t previous, current, elapsed, remaining, budget=100000U;
    if (!AppTime_IsValid() || microseconds>1000U) return false;
    if (microseconds==0U) return true;
    /* Round upwards. The backend uses this for the 10 us SPI command gap. */
    remaining=((g_CoreHz/1000U)*microseconds+999U)/1000U;
    previous=S32_SysTick->CVR;
    while (remaining!=0U) {
        current=S32_SysTick->CVR;
        elapsed=previous>=current ? previous-current : previous+s_period-current;
        if (elapsed>=remaining) return true;
        remaining-=elapsed;
        previous=current;
        if (--budget==0U) { s_configured=false; return false; }
    }
    return true;
}
