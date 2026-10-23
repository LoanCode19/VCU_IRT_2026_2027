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

#endif // TEST_HOST_FREERTOS_H
