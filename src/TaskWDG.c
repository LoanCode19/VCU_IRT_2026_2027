#ifdef USE_WDG
#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"
#include "project_config.h"

#include "console.h"
#include "TaskWDG.h"
#include "utils.h"

#include "stm32Interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <semphr.h>

WDGTimoutTable wdgTimeoutTable;

static bool checkTasksTimeout(void);

void TaskWDG(void *pvParameters)
{
    (void)pvParameters; // les signes de vie arrivent par la table wdgLastFeed (utils.c)

    TickType_t timeNow = xTaskGetTickCount();

    while (1)
    {
        vTaskDelayUntil(&timeNow, WDG_PERIOD);
        console_print("------(B)WDG------\n");

        if (checkTasksTimeout())
        {
            refreshIWDG(); // toutes les taches sont a l'heure
        }
        else
        {
            // Une tache est en retard : plus de rafraichissement, l'IWDG va redemarrer la carte
            console_print("WDG: tache en retard\n");
        }
        console_print("------(E)WDG------\n\n");
    }
}

static bool checkTasksTimeout(void)
{
    for (int i = 0; i < MAX_TASKS; i++)
    {
        if (wdgTimeoutTable[i] == 0)
        {
            continue; // tache non enregistree : pas surveillee
        }
        // Le signe de vie est lu avant l'heure : une tache plus prioritaire peut en ecrire un entre les
        // deux, et il ne doit pas paraitre venir du futur. Jamais nourri (0) : le delai court depuis t = 0.
        TickType_t lastFeed = wdgLastFeed[i];
        if ((TickType_t)(xTaskGetTickCount() - lastFeed) > wdgTimeoutTable[i])
        {
            return false; // Task i has timed out
        }
    }
    return true; // All tasks are within their timeout limits
}
#endif