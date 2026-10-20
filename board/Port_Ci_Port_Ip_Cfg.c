/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
!!GlobalInfo
product: Pins v10.0
processor: S32K144
package_id: S32K144_LQFP100
mcu_data: PlatformSDK_S32K1_2022_02
processor_version: 0.0.0
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */
 

#include "Port_Ci_Port_Ip_Cfg.h"

/* clang-format off */
/*
 * TEXT BELOW IS USED AS SETTING FOR TOOLS *************************************
BOARD_InitPins:
- options: {callFromInitBoot: 'true', coreID: core0}
- pin_list:
  - {pin_num: '27', peripheral: LPSPI0, signal: 'pcs, 0', pin_signal: PTB5, direction: OUTPUT}
  - {pin_num: '48', peripheral: LPSPI0, signal: 'sck, sck', pin_signal: PTB2, direction: OUTPUT}
  - {pin_num: '47', peripheral: LPSPI0, signal: sin, pin_signal: PTB3, direction: INPUT}
  - {pin_num: '28', peripheral: LPSPI0, signal: sout, pin_signal: PTB4, direction: OUTPUT}
  - {pin_num: '9', peripheral: CAN0, signal: 'rxd, rxd', pin_signal: PTE4, direction: INPUT}
  - {pin_num: '8', peripheral: CAN0, signal: 'txd, txd', pin_signal: PTE5, direction: OUTPUT}
 * BE CAREFUL MODIFYING THIS COMMENT - IT IS YAML SETTINGS FOR TOOLS ***********
 */
/* clang-format on */

/* Generate array of configured pin structures */
Port_Ci_Port_Ip_PinSettingsConfig g_pin_mux_InitConfigArr0[NUM_OF_CONFIGURED_PINS0] = {
    {
        .portBase        = IP_PORTB,
        .gpioBase        = NULL_PTR,
        .pinPortIdx      = 2U,
        .pullConfig      = PORT_INTERNAL_PULL_NOT_ENABLED,
        .driveStrength   = PORT_DRIVE_STRENGTH_LOW,
        .passiveFilter   = (boolean)FALSE,
        .mux             = PORT_MUX_ALT3,
        .lockRegister    = PORT_LOCK_REGISTER_DISABLED,
        .digitalFilter   = (boolean)FALSE,
    },
    {
        .portBase        = IP_PORTB,
        .gpioBase        = NULL_PTR,
        .pinPortIdx      = 3U,
        .pullConfig      = PORT_INTERNAL_PULL_NOT_ENABLED,
        .driveStrength   = PORT_DRIVE_STRENGTH_LOW,
        .passiveFilter   = (boolean)FALSE,
        .mux             = PORT_MUX_ALT3,
        .lockRegister    = PORT_LOCK_REGISTER_DISABLED,
        .digitalFilter   = (boolean)FALSE,
    },
    {
        .portBase        = IP_PORTB,
        .gpioBase        = NULL_PTR,
        .pinPortIdx      = 4U,
        .pullConfig      = PORT_INTERNAL_PULL_NOT_ENABLED,
        .driveStrength   = PORT_DRIVE_STRENGTH_LOW,
        .passiveFilter   = (boolean)FALSE,
        .mux             = PORT_MUX_ALT3,
        .lockRegister    = PORT_LOCK_REGISTER_DISABLED,
        .digitalFilter   = (boolean)FALSE,
    },
    {
        .portBase        = IP_PORTB,
        .gpioBase        = NULL_PTR,
        .pinPortIdx      = 5U,
        .pullConfig      = PORT_INTERNAL_PULL_NOT_ENABLED,
        .driveStrength   = PORT_DRIVE_STRENGTH_LOW,
        .passiveFilter   = (boolean)FALSE,
        .mux             = PORT_MUX_ALT4,
        .lockRegister    = PORT_LOCK_REGISTER_DISABLED,
        .digitalFilter   = (boolean)FALSE,
    },
    {
        .portBase        = IP_PORTE,
        .gpioBase        = NULL_PTR,
        .pinPortIdx      = 4U,
        .pullConfig      = PORT_INTERNAL_PULL_NOT_ENABLED,
        .driveStrength   = PORT_DRIVE_STRENGTH_LOW,
        .passiveFilter   = (boolean)FALSE,
        .mux             = PORT_MUX_ALT5,
        .lockRegister    = PORT_LOCK_REGISTER_DISABLED,
        .digitalFilter   = (boolean)FALSE,
    },
    {
        .portBase        = IP_PORTE,
        .gpioBase        = NULL_PTR,
        .pinPortIdx      = 5U,
        .pullConfig      = PORT_INTERNAL_PULL_NOT_ENABLED,
        .driveStrength   = PORT_DRIVE_STRENGTH_LOW,
        .passiveFilter   = (boolean)FALSE,
        .mux             = PORT_MUX_ALT5,
        .lockRegister    = PORT_LOCK_REGISTER_DISABLED,
        .digitalFilter   = (boolean)FALSE,
    },
};
/***********************************************************************************************************************
 * EOF
 **********************************************************************************************************************/
