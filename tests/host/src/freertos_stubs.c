#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "queue.h"
#include "task.h"
#include "structs.h"

typedef struct TestQueue
{
    unsigned char *buffer;
    UBaseType_t queueLength;
    UBaseType_t itemSize;
    UBaseType_t head;
    UBaseType_t tail;
    UBaseType_t count;
} TestQueue_t;

static jmp_buf *sEscapeEnv = NULL;
static TickType_t sTickCount = 0;

QueueHandle_t xQueueCreate(UBaseType_t queueLength, UBaseType_t itemSize)
{
    TestQueue_t *queue = (TestQueue_t *)calloc(1, sizeof(TestQueue_t));
    if (queue == NULL)
    {
        return NULL;
    }

    queue->buffer = (unsigned char *)calloc(queueLength, itemSize);
    if (queue->buffer == NULL)
    {
        free(queue);
        return NULL;
    }

    queue->queueLength = queueLength;
    queue->itemSize = itemSize;
    return queue;
}

BaseType_t xQueueSend(QueueHandle_t queueHandle, const void *item, TickType_t ticksToWait)
{
    TestQueue_t *queue = (TestQueue_t *)queueHandle;
    (void)ticksToWait;

    if ((queue == NULL) || (item == NULL) || (queue->count >= queue->queueLength))
    {
        return pdFAIL;
    }

    memcpy(
        queue->buffer + (queue->tail * queue->itemSize),
        item,
        queue->itemSize);

    queue->tail = (queue->tail + 1U) % queue->queueLength;
    queue->count++;
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t queueHandle, void *item, TickType_t ticksToWait)
{
    TestQueue_t *queue = (TestQueue_t *)queueHandle;
    (void)ticksToWait;

    if ((queue == NULL) || (item == NULL))
    {
        return pdFAIL;
    }

    if (queue->count == 0U)
    {
        if (sEscapeEnv != NULL)
        {
            longjmp(*sEscapeEnv, 1);
        }
        return pdFAIL;
    }

    memcpy(
        item,
        queue->buffer + (queue->head * queue->itemSize),
        queue->itemSize);

    queue->head = (queue->head + 1U) % queue->queueLength;
    queue->count--;
    return pdPASS;
}

BaseType_t xQueuePeek(QueueHandle_t queueHandle, void *item, TickType_t ticksToWait)
{
    TestQueue_t *queue = (TestQueue_t *)queueHandle;
    (void)ticksToWait;

    if ((queue == NULL) || (item == NULL) || (queue->count == 0U))
    {
        return pdFAIL;
    }

    memcpy(
        item,
        queue->buffer + (queue->head * queue->itemSize),
        queue->itemSize);
    return pdPASS;
}

void vTestEnableEscapeWhenQueueEmpty(jmp_buf *env)
{
    sEscapeEnv = env;
}

void vTestDisableEscapeWhenQueueEmpty(void)
{
    sEscapeEnv = NULL;
}

UBaseType_t uxTestQueueCount(QueueHandle_t queueHandle)
{
    TestQueue_t *queue = (TestQueue_t *)queueHandle;
    if (queue == NULL)
    {
        return 0U;
    }
    return queue->count;
}

TickType_t xTaskGetTickCount(void)
{
    sTickCount++;
    return sTickCount;
}

void vTestSetTickCount(TickType_t tickCount)
{
    sTickCount = tickCount;
}

void updateDataTimeStatus(dataMessage_t *dataMsg, int count, int maxDiff, TickType_t now)
{
    if ((dataMsg == NULL) || (count <= 0))
    {
        return;
    }

    for (int i = 0; i < count; i++)
    {
        if (dataMsg[i].timeStatus == STATUS_NOT_READY)
        {
            continue;
        }

        TickType_t timeDiff = now - dataMsg[i].timestamp;
        if (timeDiff > pdMS_TO_TICKS(maxDiff))
        {
            dataMsg[i].timeStatus = STATUS_TIMEOUT;
        }
    }
}

void vTaskDelete(void *taskToDelete)
{
    (void)taskToDelete;
    if (sEscapeEnv != NULL)
    {
        longjmp(*sEscapeEnv, 1);
    }
}
