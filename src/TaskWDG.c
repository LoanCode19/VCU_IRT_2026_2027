#ifdef USE_WDG
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "project_config.h"

#include "console.h"
#include "TaskWDG.h"
#include "utils.h"

#include "stm32Interface.h"

WDGTimoutTable wdgTimeoutTable;

static bool checkTasksTimeout(void);

// LED de vie (C7, T24, decide le 01/11/2026, revisable) : basculee toutes les HB_LED_HALF_PERIOD_MS. L'ordre passe
// par TaskIHM, sans attendre : queue pleine, il repart a la periode suivante (l'etat n'est garde qu'une fois parti).
static void HeartbeatStep(QueueHandle_t ihmQueue, TickType_t now, TickType_t *lastToggle, bool *ledOn)
{
    if ((TickType_t)(now - *lastToggle) < pdMS_TO_TICKS(HB_LED_HALF_PERIOD_MS))
    {
        return;
    }
    IHMOrder_t order = {.order = HBLed, .state = !*ledOn};
    if (xQueueSend(ihmQueue, &order, 0) == pdPASS)
    {
        *ledOn = !*ledOn;
        *lastToggle = now;
    }
}

void TaskWDG(void *pvParameters)
{
    TaskWDGParams_t *params = (TaskWDGParams_t *)pvParameters;

    TickType_t timeNow = xTaskGetTickCount();
    TickType_t lastToggle = timeNow;
    bool ledOn = false;

    while (1)
    {
        vTaskDelayUntil(&timeNow, WDG_PERIOD);

        if (checkTasksTimeout())
        {
            refreshIWDG(); // toutes les taches sont a l'heure
            // La LED de vie ne bascule que dans ce cas : elle dit que tout le VCU tourne (T24)
            HeartbeatStep(*params->xIHMQueue, timeNow, &lastToggle, &ledOn);
        }
        else
        {
            // Une tache est en retard : plus de rafraichissement, l'IWDG va redemarrer la carte
            console_print("WDG: tache en retard\n");
        }
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