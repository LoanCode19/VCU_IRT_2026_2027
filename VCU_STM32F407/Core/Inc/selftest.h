#ifndef SELFTEST_H
#define SELFTEST_H

// Autotest de la carte nue (decision P10 : pas encore d'electronique). Choisir le mode ici, compiler
// en Debug, flasher, puis lire la console SWV, port ITM 1 : un onglet a part des traces du VCU
// (port 0), qu'on peut decocher pour qu'elles ne ralentissent pas la carte. Remettre 0 avant de commiter.
//   0 : desactive. Toujours 0 dans le depot (verifie par le test C4b)
//   1 : observation. Une ligne par seconde (compteurs CAN, tas minimum). L'autotest n'envoie rien, mais
//       le VCU emet sa consigne toutes les 10 ms (R1) : sans transceiver, chaque emission echoue (bus-off repetes)
//   2 : CAN en boucle interne. La carte s'envoie les trames des capteurs et de l'onduleur ; le VCU
//       emet sa consigne 0x0A4, recue en retour (essais C-C1a et consigne)
//   3 : CAN en mode normal, sans transceiver. Chaque emission echoue : bus-off, puis reprise (C-C6a)
//   4 : watchdog. Apres 5 s, la tache d'autotest accapare le processeur : l'IWDG redemarre la carte
#ifndef VCU_SELFTEST
#define VCU_SELFTEST 0
#endif

#define SELFTEST_OBSERVE 1
#define SELFTEST_CAN_LOOPBACK 2
#define SELFTEST_CAN_NO_TRANSCEIVER 3
#define SELFTEST_WATCHDOG 4

#include "structs.h"

// Salve des modes 2 et 3, toutes les 10 ms : APPS a 40 %, roues a l'arret, onduleur a 40 degres et
// 350 V, comme le test E-B2a de l'emulateur. Sans HAL : testee sur PC (AT-a).
#define SELFTEST_FRAME_COUNT 8U
void SelfTest_BuildFrames(CanFrame_t frames[SELFTEST_FRAME_COUNT]);

#ifndef SELFTEST_HOST
#include "main.h"

// Appelee dans MX_CAN1_Init(), avant le demarrage du CAN : boucle interne en mode 2, sinon rien
void SelfTest_ConfigureCan(CAN_HandleTypeDef *hcan);

// Appelee par StartDefaultTask() : cree la tache d'autotest, sauf si VCU_SELFTEST vaut 0
void SelfTest_Start(void);
#endif

#endif // SELFTEST_H
