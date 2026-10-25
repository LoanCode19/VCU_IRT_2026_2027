#ifndef TEST_HOST_SEMPHR_H
#define TEST_HOST_SEMPHR_H

#include "FreeRTOS.h"

// Seulement les types : aucun test n'utilise de semaphore (TaskCanRX.h declare xCanRxSemaphore)
typedef void *SemaphoreHandle_t;
typedef struct
{
    int unused;
} StaticSemaphore_t;

#endif // TEST_HOST_SEMPHR_H
