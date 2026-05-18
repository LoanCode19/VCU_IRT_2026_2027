#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "TaskDigitalSignals.h"

void TaskDigitalSignals(void *pvParameters)
{
    QueueHandle_t *xDSQueue = (QueueHandle_t *)pvParameters;
    DigitalOrder_t orderMsg = {0};

    if ((xDSQueue == NULL) || (*xDSQueue == NULL))
    {
        // vTaskDelete(NULL);
    }

    for (;;)
    {
        if (xQueueReceive(*xDSQueue, &orderMsg, portMAX_DELAY) == pdPASS)
        {
            // Queue is consumed for now, digital output handling will be added later.
            (void)orderMsg.order;
            (void)orderMsg.state;
        }
    }
}
