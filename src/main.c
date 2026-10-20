/**
 * CAN0 and LPSPI0 demo using CanDriver and SpiDrv, foreground polling only.
 * SPI: PTB2=SCK, PTB3=MISO, PTB4=MOSI, PTB5=PCS0; mode 0, 100 kHz.
 * CAN: PTE4=RX, PTE5=TX; normal mode, Classic CAN, 500 kbit/s at 48 MHz.
 * Normal CAN requires a transceiver, terminated bus and an acknowledging node.
 *
 * The unchanged CAN wrapper requires a generated LOOPBACK template and copies
 * it privately to select runtime mode. The current generated NORMAL template
 * is rejected with CAN_STATUS_CONFIG_ERROR. SPI still runs if CAN init fails.
 * SPI packet: A5 5A seq[4] value[2] 01 xor[1], little endian.
 * CAN TX: ID 0x123, DLC 8, 11 22 33 44 55 66 77 88 on the first send;
 * last byte increases only after the driver accepts a new frame.
 * CAN RX: standard data frame ID 0x321, stored in g_CanLastRx.
 *
 * APP_CYCLE_ROUNDS is a loop count, not a calibrated time. OsIf currently uses
 * DUMMY timeout counters. Blocking SPI can delay CAN polling; with one CAN RX
 * mailbox this demo cannot guarantee lossless reception on a busy bus.
 */
#include "Clock_Ip.h"
#include "Port_Ci_Port_Ip.h"
#include "Port_Ci_Port_Ip_Cfg.h"
#include "IntCtrl_Ip.h"
#include "IntCtrl_Ip_Cfg.h"
#include "OsIf.h"
#include "SpiDrv.h"
#include "SpiDrv_PBcfg.h"
#include "can_driver.h"

#define APP_SPI_LENGTH       (10U)
#define APP_SPI_TIMEOUT_US   (100000UL)
#define APP_CYCLE_ROUNDS     (50000UL)
#define APP_CAN_TX_ID        (0x123U)
#define APP_CAN_RX_ID        (0x321U)

static const Can_Config App_CanConfig = {APP_CAN_RX_ID, CAN_MODE_NORMAL};
static boolean App_CanTxPending = FALSE;

/* Stage: 1=clock, 2=pins, 3=IntCtrl, 4=SPI init, 5=CAN init, 6=CAN start,
 * 7=foreground loop. Common init errors halt at the failed stage.
 */
volatile uint32 g_InitStage = 0U;
volatile Clock_Ip_StatusType g_ClockStatus;
volatile Port_Ci_Port_Ip_PortStatusType g_PortStatus;
volatile IntCtrl_Ip_StatusType g_IntCtrlStatus;
volatile boolean g_SpiReady = FALSE;
volatile boolean g_CanReady = FALSE;
volatile SpiDrv_StatusType g_SpiInitStatus = SPIDRV_NOT_INIT;
volatile SpiDrv_StatusType g_LastSpiStatus = SPIDRV_NOT_INIT;
volatile SpiDrv_StatusType g_SpiClearStatus = SPIDRV_OK;
volatile Can_Status g_CanInitStatus = CAN_STATUS_BAD_STATE;
volatile Can_Status g_CanStartStatus = CAN_STATUS_BAD_STATE;
volatile Can_Status g_CanTxStatus = CAN_STATUS_NO_TRANSFER;
volatile Can_Status g_CanRxStatus = CAN_STATUS_NO_DATA;
volatile Can_Status g_CanDiagnosticsStatus = CAN_STATUS_BAD_STATE;
volatile Can_DriverState g_CanState = CAN_DRIVER_UNINITIALIZED;
volatile Can_Diagnostics g_CanDiagnostics;
volatile Can_Frame g_CanLastTx;
volatile Can_Frame g_CanLastRx;
volatile uint8 g_SpiTx[APP_SPI_LENGTH];
volatile uint8 g_SpiRx[APP_SPI_LENGTH];
volatile uint32 g_Iterations = 0U;
volatile uint32 g_SpiOk = 0U;
volatile uint32 g_SpiErrors = 0U;
volatile uint32 g_CanTxAccepted = 0U;
volatile uint32 g_CanTxCompleted = 0U;
volatile uint32 g_CanTxErrors = 0U;
volatile uint32 g_CanRxCount = 0U;
volatile uint32 g_CanRxOverruns = 0U;
volatile uint32 g_CanRxErrors = 0U;

static void App_UpdateCanDiagnostics(void)
{
    Can_Diagnostics snapshot;
    g_CanState = CanDriver_GetState();
    if (g_CanState == CAN_DRIVER_RUNNING)
    {
        g_CanDiagnosticsStatus = CanDriver_PollDiagnostics();
    }
    if (CanDriver_GetDiagnostics(&snapshot) == CAN_STATUS_OK)
    {
        g_CanDiagnostics = snapshot;
    }
    g_CanReady = (g_CanState == CAN_DRIVER_RUNNING) ? TRUE : FALSE;
}

static void App_ServiceCan(void)
{
    Can_Status status;
    Can_Frame received;
    if (CanDriver_GetState() != CAN_DRIVER_RUNNING)
    {
        App_UpdateCanDiagnostics();
        return;
    }
    if (App_CanTxPending == TRUE)
    {
        status = CanDriver_PollTx();
        g_CanTxStatus = status;
        if (status == CAN_STATUS_OK)
        {
            App_CanTxPending = FALSE;
            g_CanTxCompleted++;
        }
        else if (status != CAN_STATUS_BUSY)
        {
            App_CanTxPending = FALSE;
            g_CanTxErrors++;
        }
    }
    if (CanDriver_GetState() == CAN_DRIVER_RUNNING)
    {
        status = CanDriver_PollRx(&received);
        g_CanRxStatus = status;
        if ((status == CAN_STATUS_OK) || (status == CAN_STATUS_RX_OVERRUN))
        {
            g_CanLastRx = received;
            g_CanRxCount++;
            if (status == CAN_STATUS_RX_OVERRUN) { g_CanRxOverruns++; }
        }
        else if (status != CAN_STATUS_NO_DATA) { g_CanRxErrors++; }
    }
    App_UpdateCanDiagnostics();
}

static void App_TransferSpi(uint32 sequence)
{
    uint8 tx[APP_SPI_LENGTH];
    uint8 rx[APP_SPI_LENGTH] = {0U};
    uint8 i;
    tx[0] = 0xA5U;
    tx[1] = 0x5AU;
    tx[2] = (uint8)sequence;
    tx[3] = (uint8)(sequence >> 8U);
    tx[4] = (uint8)(sequence >> 16U);
    tx[5] = (uint8)(sequence >> 24U);
    /* Example signal, 0..1000; replace with a sensor value if needed. */
    tx[6] = (uint8)(sequence % 1001U);
    tx[7] = (uint8)((sequence % 1001U) >> 8U);
    tx[8] = 0x01U;
    tx[9] = 0U;
    for (i = 0U; i < (APP_SPI_LENGTH - 1U); i++) { tx[9] ^= tx[i]; }
    for (i = 0U; i < APP_SPI_LENGTH; i++)
    {
        g_SpiTx[i] = tx[i];
    }
    g_LastSpiStatus = SpiDrv_SyncTransfer(SPIDRV_DEV_LPSPI0_MODE0,
                                        tx, rx, APP_SPI_LENGTH,
                                        APP_SPI_TIMEOUT_US);
    if (g_LastSpiStatus == SPIDRV_OK)
    {
        for (i = 0U; i < APP_SPI_LENGTH; i++) { g_SpiRx[i] = rx[i]; }
        g_SpiOk++;
    }
    else
    {
        /* Keep the last successful RX snapshot; do not claim failed RX valid. */
        g_SpiErrors++;
        g_SpiClearStatus = SpiDrv_ClearFault(SPIDRV_UNIT_LPSPI0);
    }
}

static void App_TransmitCan(void)
{
    Can_Frame frame = {0};
    Can_Status status;
    if ((CanDriver_GetState() != CAN_DRIVER_RUNNING) ||
        (App_CanTxPending == TRUE) || (g_CanDiagnostics.bus_off_seen != 0U))
    {
        return;
    }
    frame.id = APP_CAN_TX_ID;
    frame.dlc = 8U;
    frame.data[0] = 0x11U;
    frame.data[1] = 0x22U;
    frame.data[2] = 0x33U;
    frame.data[3] = 0x44U;
    frame.data[4] = 0x55U;
    frame.data[5] = 0x66U;
    frame.data[6] = 0x77U;
    frame.data[7] = (uint8)(0x88U + g_CanTxAccepted);
    status = CanDriver_Transmit(&frame);
    g_CanTxStatus = status;
    if (status == CAN_STATUS_OK)
    {
        App_CanTxPending = TRUE;
        g_CanLastTx = frame;
        g_CanTxAccepted++;
    }
    else if (status != CAN_STATUS_BUSY) { g_CanTxErrors++; }
}

int main(void)
{
    uint32 rounds = 0U;
    g_InitStage = 1U;
    g_ClockStatus = Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
    if (g_ClockStatus != CLOCK_IP_SUCCESS) { for (;;) { } }
    OsIf_Init(NULL_PTR);

    g_InitStage = 2U;
    g_PortStatus = Port_Ci_Port_Ip_Init(NUM_OF_CONFIGURED_PINS0,
                                      g_pin_mux_InitConfigArr0);
    if (g_PortStatus != PORT_CI_PORT_SUCCESS) { for (;;) { } }

    g_InitStage = 3U;
    g_IntCtrlStatus = IntCtrl_Ip_Init(&IntCtrlConfig_0);
    if (g_IntCtrlStatus != INTCTRL_IP_STATUS_SUCCESS) { for (;;) { } }

    g_InitStage = 4U;
    g_SpiInitStatus = SpiDrv_Init(&SpiDrv_Config);
    g_SpiReady = (g_SpiInitStatus == SPIDRV_OK) ? TRUE : FALSE;

    g_InitStage = 5U;
    g_CanInitStatus = CanDriver_Init(&App_CanConfig);
    if (g_CanInitStatus == CAN_STATUS_OK)
    {
        g_InitStage = 6U;
        g_CanStartStatus = CanDriver_Start();
    }
    App_UpdateCanDiagnostics();

    g_InitStage = 7U;
    for (;;)
    {
        App_ServiceCan();
        if (rounds == 0U)
        {
            if (g_SpiReady == TRUE) { App_TransferSpi(g_Iterations); }
            /* Poll again after the blocking SPI transfer before queuing TX. */
            App_ServiceCan();
            App_TransmitCan();
            g_Iterations++;
            rounds = APP_CYCLE_ROUNDS;
        }
        rounds--;
    }
}
