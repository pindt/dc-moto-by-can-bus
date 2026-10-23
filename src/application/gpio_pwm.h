#ifndef GPIO_PWM_H
#define GPIO_PWM_H

#include <stdbool.h>
#include <stdint.h>

/* PTD15 stays GPIO. The existing 1 ms SysTick advances a four-tick pattern:
 * 25% = HIGH/LOW/LOW/LOW, 50% = HIGH/HIGH/LOW/LOW, 75% = HIGH/HIGH/HIGH/LOW.
 * No separate LPIT/FTM timer is used; timing is approximate. */
#define GPIO_PWM_FREQUENCY_HZ UINT32_C(250)

/* Call after AppTime_Init. Leaves the pin LOW.
 * May be called again while clearing a fault; it always stops first. */
bool GpioPwm_Init(void);

/* Only 0, 250, 500, 750 and 1000 permille are accepted.
 * An invalid request stops the output. */
bool GpioPwm_SetDuty(uint16_t duty);

/* Safe before Init too. Cancels the pattern before driving the GPIO LOW. */
void GpioPwm_Stop(void);

/* Checks the stored setting and fixed levels for 0%/100%; does not measure
 * the timer frequency or the alternating GPIO waveform. */
bool GpioPwm_Matches(uint16_t duty);

/* Called only by SysTick_Handler(), once per millisecond. */
void GpioPwm_Tick(void);

#endif
