#ifndef TRVP_H
#define TRVP_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "structs.h"

void TaskRawValuesProcessing(void *pvParameters);

typedef struct
{
    uint32_t speed; // vitesse du vehicule, en mm/s (D7)

    dataMessage_t wheelSpeeds[4];
    dataMessage_t appsValues[2];
    dataMessage_t brake; // frein, trame d'etat du vehicule (T19)

    bool brakePedalPressed; // frein appuye, ou signal du frein douteux (S2)
} TRVPState_t;

typedef struct
{
    QueueHandle_t *xTRVPQueue;
    QueueHandle_t *xMainQueue;
    TaskId taskId;

} TRVPParams_t;

#endif // TRVP_H
