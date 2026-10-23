#include "hb2000_io.h"
#include "app_time.h"
#include "SpiDrv.h"
#include "SpiDrv_PBcfg.h"
#include "S32K144_PCC.h"
#include "S32K144_PORT.h"
#include "S32K144_GPIO.h"

#define HB_BIT(n) (UINT32_C(1)<<(n))
#define HB_DEVICE ((SpiDrv_DeviceId)0U)
static SpiDrv_ConfigType s_config;
static SpiDrv_DeviceConfigType s_device;
volatile SpiDrv_StatusType g_SpiInitStatus=SPIDRV_NOT_INIT;
volatile SpiDrv_StatusType g_LastSpiStatus=SPIDRV_NOT_INIT;
volatile uint32_t g_SpiActualHz;
volatile uint32_t g_SpiErrors;
volatile uint16_t g_SpiLastTx, g_SpiLastRx;
static bool s_reverse, s_running;

void HbIo_InitPins(void)
{
    IP_PCC->PCCn[PCC_PORTC_INDEX]|=PCC_PCCn_CGC_MASK;
    IP_PCC->PCCn[PCC_PORTD_INDEX]|=PCC_PCCn_CGC_MASK;
    IP_PTD->PCOR=HB_BIT(15);
    IP_PTD->PSOR=HB_BIT(1);
    IP_PORTD->PCR[1]=PORT_PCR_MUX(1U);
    IP_PORTD->PCR[15]=PORT_PCR_MUX(1U);
    IP_PTD->PDDR|=HB_BIT(1)|HB_BIT(15);
    IP_PORTC->PCR[12]=PORT_PCR_MUX(1U)|PORT_PCR_PE_MASK;
    IP_PORTC->PCR[13]=PORT_PCR_MUX(1U)|PORT_PCR_PE_MASK;
    IP_PTC->PDDR&=~(HB_BIT(12)|HB_BIT(13));
    s_running=false;
    s_reverse=false;
}
bool HbIo_InitSpi(void)
{
    uint32 actual=0U;
    s_config=SpiDrv_Config;
    s_device=SpiDrv_Config.Devices[SPIDRV_DEV_LPSPI0_MODE0];
    s_device.OverrideMask=(uint8)(SPIDRV_OVR_BUS|SPIDRV_OVR_FRAME|
                                 SPIDRV_OVR_LSB|SPIDRV_OVR_DEFAULT);
    s_device.BaudHz=1000000U;
    s_device.Cpol=0U;
    s_device.Cpha=1U;
    s_device.FrameBits=16U;
    s_device.LsbFirst=FALSE;
    s_device.DefaultData=0U;
    s_device.Continuous=FALSE;
    s_config.DeviceCount=1U;
    s_config.Devices=&s_device;
    g_SpiInitStatus=SpiDrv_Init(&s_config);
    if (g_SpiInitStatus!=SPIDRV_OK) return false;
    g_LastSpiStatus=SpiDrv_SetBaud(HB_DEVICE,1000000U,&actual);
    g_SpiActualHz=actual;
    return g_LastSpiStatus==SPIDRV_OK;
}
void HbIo_SetRun(bool running)
{
    /* IN2 is a fixed GPIO level. No timer or PWM is used. */
    if (running) IP_PTD->PSOR=HB_BIT(15);
    else IP_PTD->PCOR=HB_BIT(15);
    IP_PORTD->PCR[15]=PORT_PCR_MUX(1U);
    s_running=running;
}
bool HbIo_InputsMatch(bool running)
{
    return HbIo_OutputMatches(s_reverse,running ? 1000U : 0U);
}
bool HbIo_SetOutput(bool reverse, uint16_t duty)
{
    if ((duty!=0U && duty!=1000U) || (reverse!=s_reverse && s_running))
        return false;
    if (reverse==s_reverse && (duty!=0U)==s_running &&
        HbIo_OutputMatches(reverse,duty)) return true;
    HbIo_SetRun(false);
    if (reverse) IP_PTD->PCOR=HB_BIT(1);
    else IP_PTD->PSOR=HB_BIT(1);
    s_reverse=reverse;
    if (duty==1000U) HbIo_SetRun(true);
    return HbIo_OutputMatches(reverse,duty);
}
bool HbIo_OutputMatches(bool reverse, uint16_t duty)
{
    uint32_t expected=(reverse ? 0U : HB_BIT(1))|(duty==1000U ? HB_BIT(15) : 0U);
    return (duty==0U || duty==1000U) &&
           (IP_PORTD->PCR[1]&PORT_PCR_MUX_MASK)==PORT_PCR_MUX(1U) &&
           (IP_PORTD->PCR[15]&PORT_PCR_MUX_MASK)==PORT_PCR_MUX(1U) &&
           (IP_PTD->PDDR&(HB_BIT(1)|HB_BIT(15)))==(HB_BIT(1)|HB_BIT(15)) &&
           (IP_PTD->PDOR&(HB_BIT(1)|HB_BIT(15)))==expected;
}
bool HbIo_StartPressed(void) { return (IP_PTC->PDIR&HB_BIT(12))!=0U; }
bool HbIo_StopPressed(void) { return (IP_PTC->PDIR&HB_BIT(13))!=0U; }
HbIo_Result HbIo_Transfer(uint16_t command, uint16_t *reply)
{
    /* RTD 16-bit frames use aligned native halfwords; length is still bytes. */
    uint16 tx=(uint16)command, rx=0U;
    if (!AppTime_IsValid()) return HB_IO_TIME_ERROR;
    g_SpiLastTx=command;
    g_LastSpiStatus=SpiDrv_SyncTransfer(HB_DEVICE,(const uint8 *)&tx,
                                       (uint8 *)&rx,2U,100000U);
    if (g_LastSpiStatus!=SPIDRV_OK) {
        ++g_SpiErrors;
        return HB_IO_SPI_ERROR;
    }
    /* OSIF uses a dummy timeout counter. The transfer is bounded by polling
     * budget; application settling/telemetry use the real SysTick clock. */
    if (!AppTime_DelayUs(10U)) return HB_IO_TIME_ERROR;
    g_SpiLastRx=(uint16_t)rx;
    *reply=(uint16_t)rx;
    return HB_IO_OK;
}
HbIo_Result HbIo_Read(uint16_t command, uint16_t *value)
{
    uint16_t reply;
    HbIo_Result result=HbIo_Transfer(command,&reply);
    if (result!=HB_IO_OK) return result;
    result=HbIo_Transfer(command,&reply);
    if (result!=HB_IO_OK) return result;
    if ((reply&0xE000U)!=command) return HB_IO_PROTOCOL_ERROR;
    *value=reply;
    return HB_IO_OK;
}
