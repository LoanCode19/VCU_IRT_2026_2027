#ifndef TEST_HOST_SEMPHR_H
#define TEST_HOST_SEMPHR_H

#include "FreeRTOS.h"

// Aucun test n'utilise de semaphore. Le mutex de src/console.c (R7) est seulement declare, pour la
// verification statique : le banc remplace console.c par console_stub.c.
typedef void *SemaphoreHandle_t;
typedef struct
{
    int unused;
} StaticSemaphore_t;

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *mutexBuffer);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);

#endif // TEST_HOST_SEMPHR_H
