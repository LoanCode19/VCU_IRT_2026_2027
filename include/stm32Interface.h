#ifndef STM32_INTERFACE_H
#define STM32_INTERFACE_H

// Fonctions fournies par la plateforme : la carte (VCU_STM32F407/Core/Src/main.c), l'emulateur
// (Emulators/FreeRTOS_PC) ou le banc de test (tests/host/src/can_stub.c)

#include <stdbool.h>

#include "structs.h"

void refreshIWDG(void);

// Emet une trame sur le bus CAN. Renvoie false si elle n'a pas pu partir (boites d'emission pleines).
bool CanTx_Send(const CanFrame_t *frame);

#endif // STM32_INTERFACE_H
