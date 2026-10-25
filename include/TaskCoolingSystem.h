#ifndef TASK_COOLING_SYSTEM_H
#define TASK_COOLING_SYSTEM_H

#include "FreeRTOS.h"
#include "queue.h"
#include "structs.h"

typedef struct
{
    QueueHandle_t *xCoolingQueue;
    QueueHandle_t *xWDGQueue;
    TaskId taskId;
} CoolingParams_t;

void TaskCoolingSystem(void *pvParameters);

#endif // TASK_COOLING_SYSTEM_H
