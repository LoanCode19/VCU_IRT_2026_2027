#ifdef USE_WDG
#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"
#include "project_config.h"

#include "console.h"
#include "TaskWDG.h"

#include "stm32Interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <semphr.h>

WDGTimoutTable wdgTimeoutTable;
bool checkTasksTimeout(const WdgMessage_t taskMsg[MAX_TASKS]);

void TaskWDG(void *pvParameters)
{
    WdgParams_t *params = (WdgParams_t *)pvParameters;
    QueueHandle_t *xWDGQueue = params->xWDGQueue;

    TickType_t timeNow = xTaskGetTickCount();

    WdgMessage_t taskStates[MAX_TASKS];
    for (int i = 0; i < MAX_TASKS; i++)
    {
        taskStates[i] = (WdgMessage_t){.taskId = -1};
    }

    while (1)
    {
        vTaskDelayUntil(&timeNow, WDG_TIMEOUT);
        console_print("------(B)WDG------\n");

        WdgMessage_t wdgMsg = {0, 0};

        // Envoyer à la queue appropriée selon l'ID avec switch
        while ((xQueueReceive(*xWDGQueue, &wdgMsg, pdMS_TO_TICKS(0))) == pdPASS)
        {

            switch ((TaskId)wdgMsg.taskId)
            {
            // Wheel speeds went to Main Task
            case TaskTRVPId:
                taskStates[TaskTRVPId] = wdgMsg;
                break;
            case TaskIHMId:
                taskStates[TaskIHMId] = wdgMsg;
                break;
            case TaskCanRxId:
                taskStates[TaskCanRxId] = wdgMsg;
                break;
            case TaskCanTxId:
                taskStates[TaskCanTxId] = wdgMsg;
                break;
            case TaskMainId:
                taskStates[TaskMainId] = wdgMsg;
                break;
            default:
                break;
            }
        }
        if (!checkTasksTimeout(taskStates))
        {
#ifdef LINUX
            console_print("one task has timed out\n");
#else
            refreshIWDG()

#endif
        }
        console_print("------(E)WDG------\n\n");
    }
}

bool checkTasksTimeout(const WdgMessage_t taskMsg[MAX_TASKS])
{
    TickType_t timeNow = xTaskGetTickCount();
    for (int i = 0; i < MAX_TASKS; i++)
    {
        printf("Task %d last fed at %lu ticks, now: %lu\n", i, taskMsg[i].timestamp, timeNow);
        if (taskMsg[i].taskId == -1) // task never fed the watchdog
        {
            if (timeNow > wdgTimeoutTable[i])
            {
                return false; // Task i has never fed the watchdog and has timed out
            }
        }
        else if ((timeNow - taskMsg[i].timestamp) > wdgTimeoutTable[i])
        {
            return false; // Task i has timed out
        }
    }
    return true; // All tasks are within their timeout limits
}
#endif