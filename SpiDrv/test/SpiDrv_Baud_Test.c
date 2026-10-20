#include <stdio.h>
#include "SpiDrv_Baud.h"
static int fails = 0;
static void T(const char *n, uint32 f, uint32 b, uint32 c2k, uint32 k2c, uint32 c2c, boolean m,
              SpiDrv_StatusType exp, int ep, int ed, uint32 eact)
{
    SpiDrv_BaudResultType r;
    SpiDrv_StatusType s = SpiDrv_Baud_Calc(f, b, c2k, k2c, c2c, m, &r);
    int ok = (s == exp);
    if (ok && s == SPIDRV_OK && m) ok = (r.Prescale == ep) && (r.SckDiv == ed) && (r.ActualHz == eact) && (r.ActualHz <= b);
    printf("%-34s st=%d p=%u div=%u pcssck=%u sckpcs=%u dbt=%u act=%u  %s\n", n, s, r.Prescale, r.SckDiv,
           r.PcsSck, r.SckPcs, r.Dbt, r.ActualHz, ok ? "OK" : "FAIL");
    if (!ok) fails++;
}
int main(void)
{
    T("48MHz -> 1MHz",       48000000, 1000000, 100, 100, 500, TRUE, SPIDRV_OK, 0, 46, 1000000);
    T("48MHz -> 100kHz",     48000000, 100000,  100, 100, 500, TRUE, SPIDRV_OK, 1, 238, 100000);
    T("48MHz -> 10kHz",      48000000, 10000,   0, 0, 0,       TRUE, SPIDRV_OK, 5, 148, 10000);
    T("48MHz -> 24MHz (max)",48000000, 24000000,0, 0, 0,       TRUE, SPIDRV_OK, 0, 0, 24000000);
    T("48MHz -> 30MHz (clamp)",48000000,30000000,0, 0, 0,      TRUE, SPIDRV_OK, 0, 0, 24000000);
    T("48MHz -> 3MHz (ceil)",48000000, 3100000, 0, 0, 0,       TRUE, SPIDRV_OK, 0, 14, 3000000);
    T("baud too low (1Hz)",  48000000, 1,       0, 0, 0,       TRUE, SPIDRV_BAD_PARAM, 0,0,0);
    T("zero baud",           48000000, 0,       0, 0, 0,       TRUE, SPIDRV_BAD_PARAM, 0,0,0);
    T("zero clock",          0, 1000000,        0, 0, 0,       TRUE, SPIDRV_BAD_PARAM, 0,0,0);
    T("delay too long",      48000000, 1000000, 100000, 0, 0,  TRUE, SPIDRV_BAD_PARAM, 0,0,0);
    T("slave ok 1MHz",       48000000, 1000000, 0, 0, 0,       FALSE, SPIDRV_OK, 0,0,0);
    T("slave clk too slow",  1500000,  1000000, 0, 0, 0,       FALSE, SPIDRV_CLOCK_ERROR, 0,0,0);
    printf("fails=%d\n", fails);
    return fails;
}
