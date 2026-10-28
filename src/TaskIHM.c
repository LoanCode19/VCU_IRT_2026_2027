#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "structs.h"
#include "queueHandles.h"
#include "utils.h"
#include "TaskIHM.h"

// Transmet l'ordre a TaskDigitalSignals, dans le format que xDSQueue transporte
static void SendDSOrder(QueueHandle_t queueDS, Order_t order, bool state)
{
    DigitalOrder_t msg = {.order = order, .state = state};
    (void)xQueueSend(queueDS, &msg, DS_SEND_TIMEOUT);
}

void TaskIHM(void *pvParameters)
{
    TaskIHMParam_t *params = (TaskIHMParam_t *)pvParameters;
    QueueHandle_t *queueIHM = params->xIHMQueue;
    QueueHandle_t *queueDS = params->xDSQueue;
    TaskId taskId = params->taskId;

    for (;;)
    {
        IHMOrder_t order;
        if (xQueueReceive(*queueIHM, &order, IHM_TIMEOUT) == pdPASS)
        {
            SendDSOrder(*queueDS, order.order, order.state);
        }

        wdgFeedNow(taskId);
    }
}
