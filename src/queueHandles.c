#include "FreeRTOS.h"
#include "queue.h"

#include "queueHandles.h"

QueueHandle_t xMainQueue;
QueueHandle_t xTRVPQueue;
QueueHandle_t xIHMQueue;
QueueHandle_t xCanTxQueue;
QueueHandle_t xCanRxQueue;
QueueHandle_t xDSQueue;