#ifndef CAN_IDS_H
#define CAN_IDS_H

// Identifiants des trames CAN recues par le VCU (ID standard, 11 bits).
// Format provisoire, en attendant le DBC (D6) : une valeur de 4 octets, big-endian, en tete de trame.

#define CAN_ID_WHEEL_FR 0x20U
#define CAN_ID_WHEEL_FL 0x21U
#define CAN_ID_WHEEL_RR 0x22U
#define CAN_ID_WHEEL_RL 0x23U

#define CAN_ID_APPS1 0x30U
#define CAN_ID_APPS2 0x31U

#define CAN_ID_MOTOR_TEMP 0x40U
#define CAN_ID_INVERTER_TEMP 0x41U
#define CAN_ID_VOLTAGE 0x42U

#endif // CAN_IDS_H
