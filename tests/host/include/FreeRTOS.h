#ifndef TEST_HOST_FREERTOS_H
#define TEST_HOST_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS pdTRUE
#define pdFAIL pdFALSE

#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

// Valeur de la carte (VCU_STM32F407/Core/Inc/FreeRTOSConfig.h), utilisee par MainApp()
#define configMINIMAL_STACK_SIZE ((uint16_t)128)

// Sur la carte, configASSERT bloque le processeur. Ici : message et arret du test (freertos_stubs.c).
void vTestAssertFailed(const char *file, int line);
#define configASSERT(x)                        \
    if ((x) == 0)                              \
    {                                          \
        vTestAssertFailed(__FILE__, __LINE__); \
    }

#endif // TEST_HOST_FREERTOS_H
