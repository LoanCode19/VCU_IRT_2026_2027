
#ifdef TEST_ISR
#include "FreeRTOS.h"
#include "structs.h"
#include "queueHandles.h"
#include <semphr.h>

void CanISREmulator(void *pvParameters)
{

    TickType_t lastWakeUp = xTaskGetTickCount();

#ifdef LINUX
    CAN_Replay_Init("./VCU_APP/dataSets/can_messages.csv");
#endif

    while (1)
    {
        dataCan canMsg = {0};
        CAN_ReadNext(&canMsg);
        console_print("------ISR driver CAN------\n");
        xQueueSend(xCanRxQueue, &canMsg, portMAX_DELAY);
        vTaskDelayUntil(&lastWakeUp, pdMS_TO_TICKS(500));
    }
}

#endif
