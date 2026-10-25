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
static void sendDSOrder(QueueHandle_t *queueDS, Order_t order, bool state)
{
    DigitalOrder_t msg = {.order = order, .state = state};
    xQueueSend(*queueDS, &msg, pdMS_TO_TICKS(5));
}

void TaskIHM(void *pvParameters)
{
    TaskIHMParam_t *params = (TaskIHMParam_t *)pvParameters;
    QueueHandle_t *queueIHM = params->xIHMQueue;
    QueueHandle_t *queueDS = params->xDSQueue;
    QueueHandle_t *xWDGQueue = params->xWDGQueue;
    TaskId taskId = params->taskId;
    (void)xWDGQueue;  /* used only when USE_WDG is defined */
    (void)taskId;
    IHMOrder_t order;

    for(;;){
        if(xQueueReceive(*queueIHM, &order, IHM_TIMEOUT) == pdPASS){
            sendDSOrder(queueDS, order.order, order.state);
        }

#ifdef USE_WDG
        wdgFeed(taskId, xTaskGetTickCount(), xWDGQueue);
#endif
    }
}
