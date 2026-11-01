#ifndef TEST_HOST_TASK_H
#define TEST_HOST_TASK_H

#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;

#define tskIDLE_PRIORITY ((UBaseType_t)0U)
#define taskSCHEDULER_RUNNING ((BaseType_t)2) // valeur de FreeRTOS 10.3.1, pour src/console.c

TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
void vTaskDelete(void *taskToDelete);
void vTestSetTickCount(TickType_t tickCount);

BaseType_t xTaskCreate(TaskFunction_t taskCode, const char *name, uint32_t stackDepth,
                       void *parameters, UBaseType_t priority, TaskHandle_t *createdTask);
void vTaskStartScheduler(void);
void vTaskDelayUntil(TickType_t *previousWakeTime, TickType_t timeIncrement);
void vTaskDelay(TickType_t ticksToDelay);
BaseType_t xTaskGetSchedulerState(void);

#endif // TEST_HOST_TASK_H
