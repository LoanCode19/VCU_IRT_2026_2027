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

// Etat d'une tache surveillee. Un champ a part plutot que taskId = -1 : une enumeration
// peut etre non signee, et -1 n'y existe pas.
typedef struct
{
    bool fed;            // la tache a deja nourri le watchdog
    TickType_t lastFeed; // heure de son dernier signe de vie
} WdgTaskState_t;

static bool checkTasksTimeout(const WdgTaskState_t taskStates[MAX_TASKS]);

void TaskWDG(void *pvParameters)
{
    WdgParams_t *params = (WdgParams_t *)pvParameters;
    QueueHandle_t *xWDGQueue = params->xWDGQueue;

    TickType_t timeNow = xTaskGetTickCount();

    WdgTaskState_t taskStates[MAX_TASKS] = {0};

    while (1)
    {
        vTaskDelayUntil(&timeNow, WDG_TIMEOUT);
        console_print("------(B)WDG------\n");

        WdgMessage_t wdgMsg = {0, 0};

        // Signes de vie recus depuis la derniere periode
        while ((xQueueReceive(*xWDGQueue, &wdgMsg, pdMS_TO_TICKS(0))) == pdPASS)
        {
            if ((unsigned)wdgMsg.taskId < (unsigned)TASK_COUNT) // en non signe : rejette aussi les valeurs negatives
            {
                taskStates[wdgMsg.taskId].fed = true;
                taskStates[wdgMsg.taskId].lastFeed = wdgMsg.timestamp;
            }
        }
        if (!checkTasksTimeout(taskStates))
        {
#ifdef LINUX
            console_print("one task has timed out\n");
#else
            refreshIWDG();

#endif
        }
        console_print("------(E)WDG------\n\n");
    }
}

static bool checkTasksTimeout(const WdgTaskState_t taskStates[MAX_TASKS])
{
    TickType_t timeNow = xTaskGetTickCount();
    for (int i = 0; i < MAX_TASKS; i++)
    {
        if (wdgTimeoutTable[i] == 0)
        {
            continue; // tache non enregistree : pas surveillee
        }
        // Jamais nourri : le delai court depuis le demarrage (t = 0)
        TickType_t lastFeed = taskStates[i].fed ? taskStates[i].lastFeed : 0;
        if ((TickType_t)(timeNow - lastFeed) > wdgTimeoutTable[i])
        {
            return false; // Task i has timed out
        }
    }
    return true; // All tasks are within their timeout limits
}
#endif