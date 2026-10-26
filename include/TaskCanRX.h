#include <semphr.h>
#ifndef CAN_RX_H
#define CAN_RX_H

#include "structs.h"

void TaskCanRx(void *pvParameters);

// Compteurs de la reception CAN, lisibles au debogueur : rien n'est perdu sans etre compte
typedef struct
{
    uint32_t isrErrors; // lecture du FIFO en echec dans l'ISR de la carte
} CanRxStats_t;

extern volatile CanRxStats_t canRxStats;

// Point d'entree des trames recues, appele par l'ISR de la carte (et par l'emulateur)
void CanRx_OnFrameFromISR(const CanFrame_t *frame, BaseType_t *higherPriorityTaskWoken);

extern SemaphoreHandle_t xCanRxSemaphore;

typedef struct
{
    QueueHandle_t *xMainQueue;
    QueueHandle_t *xTRVPQueue;
    QueueHandle_t *xCanRxQueue;
    QueueHandle_t *xWDGQueue;
    TaskId taskId;
    QueueHandle_t *xCoolingQueue; // en dernier : une initialisation sans ce champ le laisse a NULL

} CanRxParams_t;

#endif