#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdint.h>

#include "queue.h"
#include "task.h"
#include "test_rtos.h"
#include "structs.h"

#define TEST_MAX_CREATED_TASKS 16U

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
static bool sTickAutoIncrement = true;

static unsigned sBlockingTimeoutsBeforeEscape = 0U;
static unsigned sBlockingTimeouts = 0U;

static unsigned sDelayCallsBeforeEscape = 0U;
static unsigned sDelayCalls = 0U;
static TestWakeHook_t sWakeHook = NULL;

static TestCreatedTask_t sCreatedTasks[TEST_MAX_CREATED_TASKS];
static unsigned sCreatedTaskCount = 0U;
static uintptr_t sStackTop = 0U;

static void EscapeIfEnabled(void)
{
    if (sEscapeEnv != NULL)
    {
        longjmp(*sEscapeEnv, 1);
    }
}

// Queue vide. Sans attente : simple echec. Avec attente : la tache se bloquerait, donc on
// sort vers le test, ou on simule un timeout si le test l'a demande.
static BaseType_t BlockOnEmptyQueue(TickType_t ticksToWait)
{
    if ((ticksToWait == 0U) || (sEscapeEnv == NULL))
    {
        return pdFAIL;
    }

    if ((ticksToWait != portMAX_DELAY) && (sBlockingTimeouts < sBlockingTimeoutsBeforeEscape))
    {
        sBlockingTimeouts++;
        sTickCount += ticksToWait;
        return pdFAIL;
    }

    longjmp(*sEscapeEnv, 1);
}

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

    // Comme FreeRTOS : on copie itemSize octets, quelle que soit la taille reelle de l'objet
    memcpy(
        queue->buffer + (queue->tail * queue->itemSize),
        item,
        queue->itemSize);

    queue->tail = (queue->tail + 1U) % queue->queueLength;
    queue->count++;
    return pdPASS;
}

BaseType_t xQueueSendFromISR(QueueHandle_t queueHandle, const void *item, BaseType_t *higherPriorityTaskWoken)
{
    if (higherPriorityTaskWoken != NULL)
    {
        *higherPriorityTaskWoken = pdFALSE;
    }
    return xQueueSend(queueHandle, item, 0U);
}

BaseType_t xQueueReceive(QueueHandle_t queueHandle, void *item, TickType_t ticksToWait)
{
    TestQueue_t *queue = (TestQueue_t *)queueHandle;

    if ((queue == NULL) || (item == NULL))
    {
        return pdFAIL;
    }

    if (queue->count == 0U)
    {
        return BlockOnEmptyQueue(ticksToWait);
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

    if ((queue == NULL) || (item == NULL))
    {
        return pdFAIL;
    }

    if (queue->count == 0U)
    {
        return BlockOnEmptyQueue(ticksToWait);
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
    sBlockingTimeouts = 0U;
    sDelayCalls = 0U;
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
    if (sTickAutoIncrement)
    {
        sTickCount++;
    }
    return sTickCount;
}

void vTestSetTickCount(TickType_t tickCount)
{
    sTickCount = tickCount;
}

void vTestSetTickAutoIncrement(bool enable)
{
    sTickAutoIncrement = enable;
}

void vTestSetBlockingTimeoutsBeforeEscape(unsigned count)
{
    sBlockingTimeoutsBeforeEscape = count;
}

void vTestSetDelayCallsBeforeEscape(unsigned count, TestWakeHook_t hook)
{
    sDelayCallsBeforeEscape = count;
    sWakeHook = hook;
}

void vTaskDelayUntil(TickType_t *previousWakeTime, TickType_t timeIncrement)
{
    if (sDelayCalls >= sDelayCallsBeforeEscape)
    {
        EscapeIfEnabled();
    }
    sDelayCalls++;

    *previousWakeTime += timeIncrement;
    sTickCount = *previousWakeTime;
    if (sWakeHook != NULL)
    {
        sWakeHook(sTickCount);
    }
}

void vTaskDelay(TickType_t ticksToDelay)
{
    TickType_t wakeTime = sTickCount;
    vTaskDelayUntil(&wakeTime, ticksToDelay);
}

BaseType_t xTaskCreate(TaskFunction_t taskCode, const char *name, uint32_t stackDepth,
                       void *parameters, UBaseType_t priority, TaskHandle_t *createdTask)
{
    (void)stackDepth;
    if (createdTask != NULL)
    {
        *createdTask = NULL;
    }
    if (sCreatedTaskCount >= TEST_MAX_CREATED_TASKS)
    {
        return pdFAIL;
    }

    // La pile descend : une adresse entre une variable locale d'ici et celle de main()
    // appartient a la pile d'un appelant (ex. une variable locale de MainApp()).
    volatile char probe = 0;
    uintptr_t address = (uintptr_t)parameters;
    bool onStack = (parameters != NULL) && (sStackTop != 0U) &&
                   (address > (uintptr_t)&probe) && (address < sStackTop);

    TestCreatedTask_t *task = &sCreatedTasks[sCreatedTaskCount++];
    task->function = taskCode;
    task->name = name;
    task->parameters = parameters;
    task->priority = priority;
    task->parametersOnStack = onStack;
    return pdPASS;
}

void vTaskStartScheduler(void)
{
    // MainApp() boucle a l'infini apres le demarrage du scheduler : on revient au test.
    EscapeIfEnabled();
}

void vTestResetCreatedTasks(void)
{
    sCreatedTaskCount = 0U;
}

unsigned uxTestCreatedTaskCount(void)
{
    return sCreatedTaskCount;
}

const TestCreatedTask_t *pxTestCreatedTask(unsigned index)
{
    return (index < sCreatedTaskCount) ? &sCreatedTasks[index] : NULL;
}

void vTestSetStackTop(const void *stackTop)
{
    sStackTop = (uintptr_t)stackTop;
}

// Copie de src/utils.c pour le test du refroidissement, qui n'embarque pas utils.c.
// Les autres secteurs testent la vraie version (TEST_LINK_REAL_UTILS).
#ifndef TEST_LINK_REAL_UTILS
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
#endif

void vTestAssertFailed(const char *file, int line)
{
    printf("ASSERT : %s:%d\n", file, line);
    exit(3);
}

void vTaskDelete(void *taskToDelete)
{
    (void)taskToDelete;
    EscapeIfEnabled();
}
