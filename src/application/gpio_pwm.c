#include "gpio_pwm.h"
#include "S32K144_GPIO.h"
#include "S32K144_PCC.h"
#include "S32K144_PORT.h"

#define GPIO_RUN_MASK (UINT32_C(1)<<15U)

static volatile bool s_initialized;
static volatile uint16_t s_duty;
static volatile uint8_t s_slot;

static uint32_t Lock(void)
{
    uint32_t saved=0U;
#if defined(__arm__) || defined(__thumb__)
    __asm volatile ("mrs %0, primask" : "=r" (saved));
    __asm volatile ("cpsid i" : : : "memory");
#endif
    return saved;
}
static void Unlock(uint32_t saved)
{
#if defined(__arm__) || defined(__thumb__)
    __asm volatile ("dsb" : : : "memory");
    __asm volatile ("msr primask, %0" : : "r" (saved) : "memory");
#else
    (void)saved;
#endif
}
static bool ValidDuty(uint16_t duty)
{ return duty<=1000U && duty%250U==0U; }

/* Interrupts are masked by the caller. No timer registers are accessed,
 * so this is also safe during early HbIo_InitPins(), before AppTime_Init(). */
static void StopLocked(void)
{
    s_duty=0U;
    s_slot=0U;
    IP_PCC->PCCn[PCC_PORTD_INDEX]|=PCC_PCCn_CGC_MASK;
    IP_PTD->PCOR=GPIO_RUN_MASK;
    IP_PORTD->PCR[15]=PORT_PCR_MUX(1U);
    IP_PTD->PDDR|=GPIO_RUN_MASK;
}
void GpioPwm_Stop(void)
{
    uint32_t saved=Lock();
    StopLocked();
    Unlock(saved);
}
bool GpioPwm_Init(void)
{
    uint32_t saved=Lock();
    StopLocked();
    s_initialized=true;
    Unlock(saved);
    return true;
}
bool GpioPwm_SetDuty(uint16_t duty)
{
    uint32_t saved=Lock();
    bool accepted=ValidDuty(duty) && (duty==0U || s_initialized);
    if (!accepted || duty==0U) StopLocked();
    else if (duty!=s_duty) {
        s_duty=duty;
        s_slot=0U;
        IP_PTD->PSOR=GPIO_RUN_MASK;
    }
    Unlock(saved);
    return accepted;
}
bool GpioPwm_Matches(uint16_t duty)
{
    uint32_t saved=Lock();
    bool matches=ValidDuty(duty) && s_duty==duty &&
                 (duty==0U || s_initialized);
    /* Intermediate levels alternate HIGH/LOW; only constant endpoints
     * have a fixed level to check. This is not a waveform measurement. */
    if (matches && (duty==0U || duty==1000U))
        matches=((IP_PTD->PDOR&GPIO_RUN_MASK)!=0U)==(duty==1000U);
    Unlock(saved);
    return matches;
}
void GpioPwm_Tick(void)
{
    uint32_t saved=Lock();
    /* Called once per existing 1 ms SysTick. Never touch shared CAN/SPI state. */
    if (s_initialized && s_duty>0U && s_duty<1000U) {
        s_slot=(uint8_t)((s_slot+1U)&3U);
        if (s_slot<s_duty/250U) IP_PTD->PSOR=GPIO_RUN_MASK;
        else IP_PTD->PCOR=GPIO_RUN_MASK;
    }
    Unlock(saved);
}
