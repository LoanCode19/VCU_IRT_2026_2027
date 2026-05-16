#include "queue.h"

#ifndef QUEUE_HANDLES_H
#define QUEUE_HANDLES_H
extern QueueHandle_t xMainQueue;
extern QueueHandle_t xTRVPQueue;
extern QueueHandle_t xIHMQueue;
extern QueueHandle_t xCanTxQueue;
extern QueueHandle_t xCanRxQueue;
extern QueueHandle_t xWDGQueue;
extern QueueHandle_t xDSQueue;

#endif // QUEUE_HANDLES_H
