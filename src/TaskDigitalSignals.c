#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "utils.h"
#include "TaskDigitalSignals.h"

static void setOutputGpio(Order_t order, bool state)
{
    switch (order)
    {
    case FaultLight:
        // TODO: drive the fault light GPIO with `state`
        break;
    case CoolingPump:
        // TODO: drive the cooling pump GPIO with `state`
        break;
    case CoolingFans:
        // TODO: drive the cooling fans GPIO with `state`
        break;
    case R2Dbuzzer:
        // TODO: drive the R2D buzzer GPIO with `state`
        break;
    case HBLed:
        // TODO: drive the heartbeat LED GPIO with `state`
        break;
    default:
        break;
    }
}

void TaskDigitalSignals(void *pvParameters)
{
    DigitalSignalsParams_t *params = (DigitalSignalsParams_t *)pvParameters;
    QueueHandle_t *xDSQueue = params->xDSQueue;
    TaskId taskId = params->taskId;
    (void)taskId;
    DigitalOrder_t orderMsg = {0};

    if ((xDSQueue == NULL) || (*xDSQueue == NULL))
    {
        // vTaskDelete(NULL);
    }

    for (;;)
    {
        // Attente limitee : un tour au moins toutes les DS_TIMEOUT, meme sans ordre, pour le watchdog
        if (xQueueReceive(*xDSQueue, &orderMsg, DS_TIMEOUT) == pdPASS)
        {
            setOutputGpio(orderMsg.order, orderMsg.state);
        }

#ifdef USE_WDG
        wdgFeed(taskId, xTaskGetTickCount());
#endif
    }
}
