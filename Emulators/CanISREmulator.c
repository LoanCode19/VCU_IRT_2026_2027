
#ifdef LINUX
#include "FreeRTOS.h"
#include "TaskCanRX.h"
#include <semphr.h>

void CanISREmulator(void *pvParameters)
{

    TickType_t lastWakeUp = xTaskGetTickCount();

    while (1)
    {
        console_print("------ISR driver CAN------\n");
        xSemaphoreGive(xCanRxSemaphore);
        xTaskDelayUntil(&lastWakeUp, pdMS_TO_TICKS(150));
    }
}

#endif