#ifndef TASK_IHM_H
#define TASK_IHM_H

#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "structs.h"

typedef struct{
    QueueHandle_t *xIHMQueue;
    QueueHandle_t *xDSQueue;
    QueueHandle_t *xWDGQueue;
    TaskId taskId;
} TaskIHMParam_t;

void TaskIHM(void *pvParameters);

#endif // TASK_IHM_H