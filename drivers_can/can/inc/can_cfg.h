#ifndef CAN_CFG_H
#define CAN_CFG_H

#include <can_types.h>

/* Fixed hardware baseline, not runtime clock/bitrate programming controls. */
#define CAN_CFG_INSTANCE             0U
#define CAN_CFG_TX_MB                0U
#define CAN_CFG_RX_MB                1U
#define CAN_CFG_MAX_STANDARD_ID      0x7FFU
#define CAN_CFG_MAX_DLC              8U
#define CAN_CFG_EXPECTED_PE_CLOCK_HZ 48000000UL
#define CAN_CFG_NOMINAL_BITRATE_BPS  500000UL
#define CAN_CFG_DEFAULT_RX_ID        0x14U

#define CAN_CONFIG_DEFAULT { CAN_CFG_DEFAULT_RX_ID, CAN_MODE_LOOPBACK }

#endif
