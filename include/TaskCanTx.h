#ifndef CAN_TX_H
#define CAN_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "structs.h"

void TaskCanTx(void *pvParameters);

// Trames de commande de l'onduleur DTI HV-550 (DTI CAN manual V2.5, ID standard)
CanFrame_t Dti_EncodeRelativeCurrent(float percent); // consigne en % des limites reglees, de 0 a 100
CanFrame_t Dti_EncodeDriveEnable(bool enable);       // conduite autorisee (R2D) ou interdite

extern volatile uint32_t canTxErrors; // consignes qui n'ont pas pu partir (boites d'emission pleines)

typedef struct
{
    QueueHandle_t *xCanTxQueue;
    QueueHandle_t *xWDGQueue;

    TaskId taskId;

} CanTxParam_t;

#endif