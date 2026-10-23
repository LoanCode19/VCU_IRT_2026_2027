#ifndef TEST_HOST_TASK_H
#define TEST_HOST_TASK_H

#include "FreeRTOS.h"

TickType_t xTaskGetTickCount(void);
void vTaskDelete(void *taskToDelete);
void vTestSetTickCount(TickType_t tickCount);

#endif // TEST_HOST_TASK_H
