#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "utils.h"
#include "TaskDigitalSignals.h"

// Etat demande de chaque sortie (index : Order_t), lisible au debogueur (Live Expressions). Les broches ne sont
// pas encore choisies (D5) : aucune GPIO n'est pilotee.
volatile bool digitalOutputs[OUTPUT_COUNT];

static void setOutputGpio(Order_t order, bool state)
{
    if ((unsigned)order < (unsigned)OUTPUT_COUNT)
    {
        digitalOutputs[order] = state; // TODO (D5) : piloter la broche de la sortie
    }
}

void TaskDigitalSignals(void *pvParameters)
{
    DigitalSignalsParams_t *params = (DigitalSignalsParams_t *)pvParameters;
    QueueHandle_t *xDSQueue = params->xDSQueue;
    TaskId taskId = params->taskId;
    DigitalOrder_t orderMsg = {0};

    for (;;)
    {
        // Attente limitee : un tour au moins toutes les DS_TIMEOUT, meme sans ordre, pour le watchdog
        if (xQueueReceive(*xDSQueue, &orderMsg, DS_TIMEOUT) == pdPASS)
        {
            setOutputGpio(orderMsg.order, orderMsg.state);
        }

        wdgFeedNow(taskId);
    }
}
