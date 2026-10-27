#ifndef TASK_DIGITAL_SIGNALS_H
#define TASK_DIGITAL_SIGNALS_H

#include "FreeRTOS.h"
#include "queue.h"
#include "structs.h"

typedef struct
{
    QueueHandle_t *xDSQueue;
    TaskId taskId;
} DigitalSignalsParams_t;

void TaskDigitalSignals(void *pvParameters);

#endif // TASK_DIGITAL_SIGNALS_H
