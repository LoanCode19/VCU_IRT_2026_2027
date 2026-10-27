#ifndef VCU_STATE_H
#define VCU_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

// Ready-to-Drive (S3, T8 revise le 27/10/2026, revisable) : fonction pure, testable sans FreeRTOS.
// Le bouton R2D est un interrupteur a accrochage (ON/OFF) : on entre en R2D sur son passage OFF -> ON pendant un
// freinage, on en sort quand il passe sur OFF, que le SDC s'ouvre, que le TS tombe ou que la trame d'etat se perd.

typedef enum
{
    VCU_TS_OFF, // TS inactif, SDC ouvert ou trame d'etat perdue
    VCU_TS_ON,  // TS actif et SDC ferme, pas en R2D
    VCU_R2D,    // les moteurs repondent a la pedale (EV 4.11.6)
} VcuState_t;

// Signaux de la trame d'etat du vehicule (T19), deja verifies par VcuInputsFromSignals()
typedef struct
{
    bool fresh;     // les quatre signaux sont arrives a temps, avec une valeur 0 ou 1
    bool tsActive;  // TS actif (precharge terminee)
    bool sdcClosed; // circuit d'arret ferme
    bool brakePressed;
    bool r2dSwitch; // interrupteur R2D sur ON (T22)
} VcuInputs_t;

typedef struct
{
    VcuState_t state;
    bool lastSwitch;     // derniere position vue ; true au demarrage et apres une trame perdue (T8)
    TickType_t r2dSince; // heure d'entree en R2D, pour le son
} VcuStateMachine_t;

typedef struct
{
    bool torqueAllowed;
    bool buzzerOn;    // son R2D (EV 4.12.1)
    bool driveEnable; // "Drive enable" du DTI (T20)
} VcuOutputs_t;

void VcuStateInit(VcuStateMachine_t *sm);
VcuOutputs_t VcuStateStep(VcuStateMachine_t *sm, const VcuInputs_t *in, TickType_t now);

// Valeurs brutes des quatre signaux -> entrees de la machine. fresh : les quatre signaux sont arrives a temps. Une
// valeur autre que 0 ou 1 rend la trame invalide, comme une trame perdue (T 11.9.2.c). Quand l'interrupteur sera lu
// sur une broche de la carte (T22), seule sa valeur changera d'origine.
VcuInputs_t VcuInputsFromSignals(bool fresh, int32_t tsActive, int32_t sdcClosed, int32_t brake, int32_t r2dSwitch);

#endif // VCU_STATE_H
