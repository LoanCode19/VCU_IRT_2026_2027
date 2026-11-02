#ifndef TASK_COOLING_SYSTEM_H
#define TASK_COOLING_SYSTEM_H

#include "FreeRTOS.h"
#include "queue.h"
#include "structs.h"

typedef struct
{
    QueueHandle_t *xCoolingQueue;
    TaskId taskId;
    QueueHandle_t *xDSQueue; // queue de TaskDigitalSignals, ou partent les ordres (Q9)
} CoolingParams_t;

void TaskCoolingSystem(void *pvParameters);

#endif // TASK_COOLING_SYSTEM_H
