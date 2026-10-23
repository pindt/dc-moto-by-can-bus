#ifndef HB2000_IO_H
#define HB2000_IO_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HB_IO_OK=0, HB_IO_SPI_ERROR, HB_IO_PROTOCOL_ERROR, HB_IO_TIME_ERROR
} HbIo_Result;

void HbIo_InitPins(void);
bool HbIo_InitPwm(void); /* After IntCtrl/AppTime init; leaves IN2 low. */
bool HbIo_InitSpi(void);
/* PTD1 = direction; PTD15 remains GPIO, switched by the 1 ms timer tick.
 * Duty is per mille: 0, 250, 500, 750 or 1000. */
bool HbIo_SetOutput(bool reverse, uint16_t duty);
bool HbIo_OutputMatches(bool reverse, uint16_t duty);
void HbIo_SetRun(bool running);
bool HbIo_InputsMatch(bool running);
bool HbIo_StartPressed(void);
bool HbIo_StopPressed(void);
HbIo_Result HbIo_Transfer(uint16_t command, uint16_t *reply);
HbIo_Result HbIo_Read(uint16_t command, uint16_t *value);
#endif
