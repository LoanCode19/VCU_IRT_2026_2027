
#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"

#include "console.h"
#include "TaskCanTx.h"
#include "my_fp.h"

void TaskCanTx(void *pvParameters)
{
    TickType_t lastWakeUp = xTaskGetTickCount();
    unsigned long i = 0;
    dataMessage_t msg = {0};

    CanTxParam_t *params = (CanTxParam_t *)pvParameters;
    QueueHandle_t *xCanTxQueue = params->xCanTxQueue;

    while (1)
    {

        msg.id++;
        msg.timestamp = xTaskGetTickCount();

        dataMessage_t msg = {0};
        BaseType_t xReturn = xQueueReceive(*xCanTxQueue, &msg, portMAX_DELAY);
        console_print("(B)------CanTX------\n");
        if (xReturn == pdPASS)
        {

            switch (msg.id)
            {
            case 0x50:
                float throttle = FP_TOFLOAT(msg.data);
                console_print("sending Throttle Order = %f to inverter\n", throttle);
                break;
            }
        }
        console_print("(E)------CanTX------\n\n");
    }
}