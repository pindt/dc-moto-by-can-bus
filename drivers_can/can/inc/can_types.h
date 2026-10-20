#ifndef CAN_TYPES_H
#define CAN_TYPES_H

#include <stdint.h>

/* Standard 11-bit Classic CAN data frames only. */
typedef struct
{
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} Can_Frame;

typedef enum
{
    CAN_MODE_LOOPBACK = 0,
    CAN_MODE_NORMAL = 1
} Can_Mode;

typedef struct
{
    uint16_t rx_id; /* Exact standard identifier accepted by RX MB1. */
    Can_Mode mode; /* Selected at Init; no mode switching while running. */
} Can_Config;

typedef enum
{
    CAN_STATUS_OK = 0,
    CAN_STATUS_BUSY,
    CAN_STATUS_NO_DATA,
    CAN_STATUS_NO_TRANSFER,
    CAN_STATUS_INVALID_ARGUMENT,
    CAN_STATUS_BAD_STATE,
    CAN_STATUS_CONFIG_ERROR,
    CAN_STATUS_TIMEOUT, /* RTD timeout; NOT a calibrated real-time deadline. */
    CAN_STATUS_HW_ERROR,
    CAN_STATUS_INVALID_FRAME,
    CAN_STATUS_RX_OVERRUN /* Output frame is valid, earlier traffic was lost. */
} Can_Status;

typedef enum
{
    CAN_DRIVER_UNINITIALIZED = 0,
    CAN_DRIVER_READY, /* Initialized, not running. */
    CAN_DRIVER_RUNNING,
    CAN_DRIVER_FAULT /* Only Deinit may touch the controller in this state. */
} Can_DriverState;

typedef enum
{
    CAN_FAULT_ACTIVE = 0,
    CAN_FAULT_PASSIVE,
    CAN_FAULT_BUS_OFF
} Can_FaultState;

/* Sequential RTD observations, not an atomic hardware snapshot. */
typedef struct
{
    uint32_t esr1;
    uint8_t tx_error_counter;
    uint8_t rx_error_counter;
    Can_FaultState fault_state;
    uint8_t tx_warning;
    uint8_t rx_warning;
    uint8_t ack_error;
    uint8_t bit0_error;
    uint8_t bit1_error;
    uint8_t crc_error;
    uint8_t form_error;
    uint8_t stuff_error;
} Can_DiagnosticSample;

typedef struct
{
    Can_Mode mode;
    Can_DriverState driver_state;
    uint8_t tx_pending;
    uint8_t valid;
    uint8_t first_error_valid;
    uint8_t first_fault_valid; /* First wrapper API failure or bus-off. */
    uint8_t first_fault_sample_valid; /* Init can fail before safe sampling. */
    uint8_t bus_off_seen;
    uint32_t sample_count; /* Saturates at UINT32_MAX. */
    uint32_t accumulated_error_events; /* ESR1 event bits only, never FLTCONF. */
    Can_Status first_fault_status;
    Can_DiagnosticSample latest;
    Can_DiagnosticSample first_error;
    Can_DiagnosticSample first_fault;
} Can_Diagnostics;

#endif
