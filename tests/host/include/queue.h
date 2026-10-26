#ifndef TEST_HOST_QUEUE_H
#define TEST_HOST_QUEUE_H

#include <setjmp.h>
#include "FreeRTOS.h"

typedef struct TestQueue *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t queueLength, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticksToWait);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *higherPriorityTaskWoken);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticksToWait);
BaseType_t xQueueReceiveFromISR(QueueHandle_t queue, void *item, BaseType_t *higherPriorityTaskWoken);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t ticksToWait);

void vTestEnableEscapeWhenQueueEmpty(jmp_buf *env);
void vTestDisableEscapeWhenQueueEmpty(void);
UBaseType_t uxTestQueueCount(QueueHandle_t queue);

#endif // TEST_HOST_QUEUE_H
