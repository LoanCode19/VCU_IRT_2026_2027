#ifndef QUEUE_HANDLES_H
#define QUEUE_HANDLES_H

#include "FreeRTOS.h"
#include "queue.h"

extern QueueHandle_t xMainQueue;
extern QueueHandle_t xTRVPQueue;
extern QueueHandle_t xIHMQueue;
extern QueueHandle_t xCanTxQueue;
extern QueueHandle_t xCanRxQueue;
extern QueueHandle_t xDSQueue;
extern QueueHandle_t xCoolingQueue;

#endif // QUEUE_HANDLES_H
