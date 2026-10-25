#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"
#include "utils.h"
#include "project_config.h"

#include "console.h"
#include "TaskCanRX.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <semphr.h>

SemaphoreHandle_t xCanRxSemaphore;

typedef enum canIds
{
    CAN_wheelFRId = 0x20,
    CAN_wheelFLId = 0x21,
    CAN_wheelRRId = 0x22,
    CAN_wheelRLId = 0x23,

    CAN_apps1Id = 0x30,
    CAN_apps2Id = 0x31,

    CAN_motor_tempId = 0x40,
    CAN_inverter_tempId = 0x41,
    CAN_voltageId = 0x42,

} CanIds;

// Le refroidissement recoit aussi les temperatures. Pointeur NULL : pas de refroidissement
// (tests qui remplissent CanRxParams_t sans ce champ).
static void SendToCooling(QueueHandle_t *xCoolingQueue, const dataMessage_t *msg)
{
    if ((xCoolingQueue != NULL) && (*xCoolingQueue != NULL))
    {
        xQueueSend(*xCoolingQueue, msg, pdMS_TO_TICKS(5));
    }
}

void TaskCanRx(void *pvParameters)
{
    CanRxParams_t *params = (CanRxParams_t *)pvParameters;
    QueueHandle_t *xMainQueue = params->xMainQueue;
    QueueHandle_t *xTRVPQueue = params->xTRVPQueue;
    QueueHandle_t *xCanRxQueue = params->xCanRxQueue;
    QueueHandle_t *xWDGQueue = params->xWDGQueue;
    QueueHandle_t *xCoolingQueue = params->xCoolingQueue;
    TaskId taskId = params->taskId;

    TickType_t timeNow = xTaskGetTickCount();

    while (1)
    {

        dataMessage_t appMsg = {0, 0};
        dataCan canMsg = {0};

        xQueuePeek(*xCanRxQueue, &canMsg, CAN_RX_TIMEOUT);
        console_print("------(B)CanRx------\n");

        // Envoyer à la queue appropriée selon l'ID avec switch
        while ((xQueueReceive(*xCanRxQueue, &canMsg, pdMS_TO_TICKS(0))) == pdPASS)
        {
            appMsg.data = canMsg.data;
            appMsg.timestamp = xTaskGetTickCount();

            switch ((CanIds)canMsg.id)
            {
            // Wheel speeds went to Main Task
            case CAN_wheelFRId:
                appMsg.id = wheelFRId;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_wheelFLId:
                appMsg.id = wheelFLId;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_wheelRRId:
                appMsg.id = wheelRRId;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_wheelRLId:
                appMsg.id = wheelRLId;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;

            case CAN_apps1Id:
                appMsg.id = apps1Id;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_apps2Id:
                appMsg.id = apps2Id;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;

            case CAN_motor_tempId:
                appMsg.id = motor_tempId;
                xQueueSend(*xMainQueue, &appMsg, pdMS_TO_TICKS(5));
                SendToCooling(xCoolingQueue, &appMsg);
                break;
            case CAN_inverter_tempId:
                appMsg.id = inverter_tempId;
                xQueueSend(*xMainQueue, &appMsg, pdMS_TO_TICKS(5));
                SendToCooling(xCoolingQueue, &appMsg);
                break;
            case CAN_voltageId:
                appMsg.id = voltageId;
                xQueueSend(*xMainQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            default:
                break;
            }
            console_print("sent appMsg id:%d data:%ld\n", appMsg.id, (long)appMsg.data);
        }

        // console_print((ledState = !ledState) ? "Led ON\n" : "Led OFF\n");

#ifdef USE_WDG
        // feed WDG
        wdgFeed(taskId, xTaskGetTickCount(), xWDGQueue);
#endif

        console_print("------(E)CanRx------\n\n");
    }
}
