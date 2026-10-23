#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>

#define CP_COMMAND_ID 0x100U
#define CP_RESPONSE_ID 0x180U
#define CP_VERSION 2U
#define CP_DLC 8U

typedef enum {
    CP_STOP=0, CP_START=1, CP_START_FORWARD=2, CP_START_REVERSE=3,
    CP_SET_DUTY=4, CP_CLEAR_FAULT=5, CP_GET_STATUS=6
} Cp_Opcode;
typedef enum {
    CP_OK=0, CP_PENDING=1, CP_COMPLETED=2,
    CP_INVALID_COMMAND=0x80, CP_INVALID_ARGUMENT=0x81,
    CP_INVALID_STATE=0x82, CP_NOT_CONFIGURED=0x83, CP_MOTOR_FAULT=0x84,
    CP_BUSY=0x85, CP_SEQUENCE_CONFLICT=0x86, CP_SAFETY_INTERLOCK=0x87,
    CP_CAN_ERROR=0x88, CP_INTERNAL_TIMEOUT=0x89, CP_STALE_SEQUENCE=0x8A
} Cp_Result;
typedef enum {
    CP_ACK_NACK=1, CP_MOTOR_STATUS=2, CP_MOTOR_ERROR=3, CP_HB_DIAGNOSTICS=4
} Cp_MessageType;
typedef enum {
    CP_UNCONFIGURED=0, CP_STOPPED=1, CP_STARTING=2,
    CP_RUNNING=3, CP_STOPPING=4, CP_FAULT=5
} Cp_State;
typedef enum {
    CP_FAULT_NONE=0, CP_FAULT_COMMUNICATION=1, CP_FAULT_BACKEND=2,
    CP_FAULT_CAN_API=3, CP_FAULT_BUS_OFF=4, CP_FAULT_RX_OVERRUN=5,
    CP_FAULT_RX_INVALID=6, CP_FAULT_QUEUE=7, CP_FAULT_TIME=8,
    CP_FAULT_BACKEND_TIMEOUT=9
} Cp_Fault;
enum {
    CP_FLAG_CONFIGURED=1U, CP_FLAG_SAFE_OFF=2U, CP_FLAG_MOCK=4U,
    CP_FLAG_REVERSE_SUPPORTED=8U, CP_FLAG_CONTROL_ALLOWED=16U,
    CP_FLAG_OPERATION_PENDING=32U, CP_FLAG_REVERSE=64U
};
typedef struct {
    uint8_t opcode;
    uint16_t argument;
    uint8_t identity[8];
} Cp_Command;
typedef struct {
    bool valid;
    bool respond; /* Enough trusted correlation bytes; not an actuation permission. */
    Cp_Result result;
    Cp_Command command;
} Cp_DecodeResult;

/* V2 RX: [opcode, argumentLo, argumentHi, 0, 0, 0, 0, 0].
 * SET_DUTY accepts 0..1000 permille and rounds to 0/250/500/750/1000.
 * No sequence, KEEPALIVE or response acknowledgement is required. */
/* Nearest 25% step; an exact halfway value rounds up. Invalid input does
 * not modify the output. The received identity bytes are never changed. */
bool CanProtocol_QuantizeDuty(uint16_t requested_duty, uint16_t *quantized_duty);
Cp_DecodeResult CanProtocol_Decode(uint16_t id, bool extended, bool remote,
                                 uint8_t dlc, const uint8_t *data);
void CanProtocol_EncodeAck(uint8_t out[8], const Cp_Command *command,
                          Cp_Result result, Cp_Fault fault);
void CanProtocol_EncodeStatus(uint8_t out[8], Cp_State state, uint8_t flags,
                             uint16_t requested_duty, Cp_Fault fault);
void CanProtocol_EncodeError(uint8_t out[8], uint8_t flags,
                            uint16_t requested_duty, Cp_Fault fault);
/* [type, valid, status LE16, config LE16, device ID low byte, version]. */
void CanProtocol_EncodeHbDiagnostics(uint8_t out[8], bool valid, uint16_t id,
                                     uint16_t status, uint16_t config);
#endif
