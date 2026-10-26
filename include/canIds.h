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

// Onduleur DTI HV-550 (D6, decide le 26/10/2026, revisable) : ID standard = type de trame x 32 + numero.
// Source : DTI CAN manual V2.5. Toutes les donnees sont en big-endian.
#define DTI_NODE_ID 4U // numero de l'onduleur, regle dans DTI CAN Tool
#define DTI_STD_ID(packet) ((((uint32_t)(packet)) << 5) | DTI_NODE_ID)

#define CAN_ID_DTI_ERPM_VOLTAGE DTI_STD_ID(0x20)    // 0x404 : ERPM, rapport cyclique, tension DC
#define CAN_ID_DTI_CURRENTS DTI_STD_ID(0x21)        // 0x424 : courants AC et DC
#define CAN_ID_DTI_TEMPERATURES DTI_STD_ID(0x22)    // 0x444 : temperatures onduleur et moteur, defaut
#define CAN_ID_DTI_STATUS DTI_STD_ID(0x24)          // 0x484 : activation, limites actives
#define CAN_ID_DTI_SET_REL_CURRENT DTI_STD_ID(0x05) // 0x0A4 : consigne en % des limites reglees
#define CAN_ID_DTI_DRIVE_ENABLE DTI_STD_ID(0x0C)    // 0x184 : conduite autorisee ou interdite

#endif // CAN_IDS_H
