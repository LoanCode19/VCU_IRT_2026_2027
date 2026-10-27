#ifndef CAN_IDS_H
#define CAN_IDS_H

// Identifiants des trames CAN du VCU (ID standard, 11 bits). Valeurs du lot 4 (26/10/2026, revisables).

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

// Capteurs, format provisoire jusqu'au DBC (D6) : une valeur de 4 octets big-endian en tete de trame.
// En ID standard, le DTI lit comme une commande toute trame de 0x020 a 0x19F qui porte son numero, et
// ses trames d'etat occupent 0x3E0 a 0x4DF : aucune autre trame dans ces deux plages (test B6g).
#define CAN_ID_APPS1 0x010U
#define CAN_ID_APPS2 0x011U

// Etat du vehicule, trame PROVISOIRE en attendant D5 (T19, decide le 27/10/2026, revisable) : toutes les 10 ms,
// 4 octets, 0 ou 1 : TS actif, SDC ferme, frein appuye, interrupteur R2D sur ON. L'interrupteur sera lu sur une
// entree numerique de la carte quand l'electronique aura choisi la broche (T22).
#define CAN_ID_VEHICLE_STATE 0x012U

#define CAN_ID_WHEEL_FR 0x500U
#define CAN_ID_WHEEL_FL 0x501U
#define CAN_ID_WHEEL_RR 0x502U
#define CAN_ID_WHEEL_RL 0x503U

#endif // CAN_IDS_H
