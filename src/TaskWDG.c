#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"
#include "project_config.h"

#include "console.h"
#include "TaskCanRX.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <semphr.h>

SemaphoreHandle_t xCanRxSemaphore;

void TaskWDG(void *pvParameters)
{
    TWdgParams_t *params = (TWdgParams_t *)pvParameters;
    QueueHandle_t *xWDGQueue = params->xWDGQueue;

    TickType_t timeNow = xTaskGetTickCount();

    TasksIds pool[5];

    while (1)
    {
        WdgMessage_t wdgMsg = {0, 0};

        // Envoyer à la queue appropriée selon l'ID avec switch
        while ((xQueueReceive(*xWDGQueue, &wdgMsg, pdMS_TO_TICKS(0))) == pdPASS)
        {

            switch ((TasksIds)wdgMsg.taskId)
            {
            // Wheel speeds went to Main Task
            case TaskTRVPId:
                break;
            case TaskIHMId:
                break;
            case TaskCanRxId:
                break;
            case TaskCanTxId:
                break;
            case TaskMainId:
                break;
            default:
                break;
            }
        }

        vTaskDelayUntil(&timeNow, WDG_TIMEOUT);
        wdgMsg.timestamp = xTaskGetTickCount();
        console_print("------(E)CanRx------\n\n");
    }
}
