#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "structs.h"
#include "queueHandles.h"
#include "utils.h"
#include "TaskIHM.h"

static void sendDSOrder(QueueHandle_t *queueDS, MessageIds id, bool state)
{
    dataMessage_t msg = {.id = id, .data = (int32_t)state, .timestamp = xTaskGetTickCount()};
    xQueueSend(*queueDS, &msg, pdMS_TO_TICKS(5));
}

static void toggleHBLED(QueueHandle_t *queueDS, bool newState)
{
    sendDSOrder(queueDS, hbledId, newState);
}

static void toggleFaultLight(QueueHandle_t *queueDS, bool newState)
{
    sendDSOrder(queueDS, faultLightId, newState);
}

static void playR2DBuzzerSound(QueueHandle_t *queueDS, bool newState)
{
    sendDSOrder(queueDS, r2dBuzzerId, newState);
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
            switch(order.order){
                case HBLed:
                    toggleHBLED(queueDS, order.state);
                    break;
                case R2Dbuzzer:
                    playR2DBuzzerSound(queueDS, order.state);
                    break;
                case FaultLight:
                    toggleFaultLight(queueDS, order.state);
                    break;
            }
        }

#ifdef USE_WDG
        wdgFeed(taskId, xTaskGetTickCount(), xWDGQueue);
#endif
    }
}
