/* Simple CAN driver smoke test for S32K144.
 *
 * CAN_TEST_LOOPBACK = 1 : internal loopback, nothing appears on the CAN pins.
 *                         Only proves the wrapper + RTD software path works.
 * CAN_TEST_LOOPBACK = 0 : NORMAL mode, frame 0x123 is sent every ~100 ms on
 *                         PTE5 (CAN0_TX). Use this to see it on the analyzer.
 *                         If no node ACKs, the controller retransmits the same
 *                         frame continuously (ACK error) - still visible on TX.
 *
 * Observe g_can in S32DS "Expressions" (or watch the LED/analyzer).
 */
#include "Mcal.h"
#include "Clock_Ip.h"
#include "can_driver.h"
#include "can_cfg.h"
#include "board_can.h"   /* BoardCan_InitPins(): PTE4=CAN0_RX, PTE5=CAN0_TX (ALT5) */
#include "S32K144_PCC.h"

#ifndef CAN_TEST_LOOPBACK
#define CAN_TEST_LOOPBACK 0
#endif

#define TX_ID        0x123U
#define RX_ID        0x321U      /* normal mode: ID we accept */
#define LOOP_ID      0x14U       /* loopback: must equal TX id */
#define DELAY_LOOPS  400000UL    /* rough gap between frames, not a precise time */

typedef struct
{
    uint32_t step;               /* 1 pins, 2 init, 3 start, 4 running, 99 failed */
    Can_Status last_status;      /* status of the last failing/most recent call */
    BoardCan_Status pins_status; /* 0 = BOARD_CAN_OK, 1 = PORTE clock disabled */
    Can_Status init_status;
    Can_Status start_status;
    uint32_t tx_accepted;
    uint32_t tx_done;
    uint32_t rx_count;
    Can_Frame last_rx;
    Can_Diagnostics diag;        /* esr1, error counters, ack_error, bus-off ... */
} CanSimple;

volatile CanSimple g_can;

static void SaveDiag(void)
{
    Can_Diagnostics d;
    if (CanDriver_GetDiagnostics(&d) == CAN_STATUS_OK) { g_can.diag = d; }
}

static void Fail(Can_Status s)
{
    g_can.last_status = s;
    g_can.step = 99U;
    SaveDiag();
    for (;;) { }                 /* stop here, inspect g_can */
}

int main(void)
{
    Can_Frame frame = { 0 };
    Can_Frame rx;
    Can_Status s;
    uint32_t i;

    Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
#if defined (FEATURE_CLOCK_IP_HAS_SPLL_CLK)
    while (CLOCK_IP_PLL_LOCKED != Clock_Ip_GetPllStatus()) { }
    Clock_Ip_DistributePll();
#endif

#if CAN_TEST_LOOPBACK
    const Can_Config cfg = { LOOP_ID, CAN_MODE_LOOPBACK };
    frame.id = LOOP_ID;
#else
    const Can_Config cfg = { RX_ID, CAN_MODE_NORMAL };
    frame.id = TX_ID;
    g_can.step = 1U;
    g_can.pins_status = BoardCan_InitPins();
    if (g_can.pins_status == BOARD_CAN_CLOCK_DISABLED)
    {
        /* Clock config did not enable PORTE: enable it here and retry. */
        IP_PCC->PCCn[PCC_PORTE_INDEX] |= PCC_PCCn_CGC_MASK;
        g_can.pins_status = BoardCan_InitPins();
    }
    if (g_can.pins_status != BOARD_CAN_OK) { Fail(CAN_STATUS_CONFIG_ERROR); }
#endif
    frame.dlc = 8U;
    for (i = 0U; i < 8U; ++i) { frame.data[i] = (uint8_t)(0x11U * (i + 1U)); }

    g_can.step = 2U;
    s = CanDriver_Init(&cfg);
    g_can.init_status = s;
    if (s != CAN_STATUS_OK) { Fail(s); }

    g_can.step = 3U;
    s = CanDriver_Start();
    g_can.start_status = s;
    if (s != CAN_STATUS_OK) { Fail(s); }

    g_can.step = 4U;
    for (;;)
    {
        /* 1. send one frame */
        s = CanDriver_Transmit(&frame);
        if (s != CAN_STATUS_OK) { Fail(s); }
        ++g_can.tx_accepted;
        ++frame.data[7];         /* change last byte so each frame is distinguishable */

        /* 2. wait for TX completion (stays BUSY while no node ACKs) */
        do
        {
            (void)CanDriver_PollDiagnostics();
            s = CanDriver_PollRx(&rx);
            if ((s == CAN_STATUS_OK) || (s == CAN_STATUS_RX_OVERRUN))
            {
                g_can.last_rx = rx;
                ++g_can.rx_count;
            }
            s = CanDriver_PollTx();
            SaveDiag();
        } while (s == CAN_STATUS_BUSY);

        if (s != CAN_STATUS_OK) { Fail(s); }
        ++g_can.tx_done;
        SaveDiag();

        /* 3. gap between frames, keep servicing RX */
        for (i = 0U; i < DELAY_LOOPS; ++i)
        {
            s = CanDriver_PollRx(&rx);
            if ((s == CAN_STATUS_OK) || (s == CAN_STATUS_RX_OVERRUN))
            {
                g_can.last_rx = rx;
                ++g_can.rx_count;
            }
        }
    }
}
