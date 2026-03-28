#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"

#include "console.h"
#include "TaskCanRX.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <semphr.h>

#ifdef LINUX
#include "CanRXDriverEmulator.h"
#else
#include "stm32f4xx_hal.h"
#endif

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

void TaskCanRx(void *pvParameters)
{

#ifdef LINUX
    CAN_Replay_Init("./VCU_APP/dataSets/can_messages.csv");
#endif

    CanRxParams_t *params = (CanRxParams_t *)pvParameters;
    QueueHandle_t *xMainQueue = params->xMainQueue;
    QueueHandle_t *xTRVPQueue = params->xTRVPQueue;
    QueueHandle_t *xCanRxQueue = params->xCanRxQueue;

    TickType_t timeNow = xTaskGetTickCount();

    while (1)
    {

        console_print("------(B)CanRx------\n");
        dataMessage_t appMsg = {0, 0};
        dataCan canMsg = {0};
        // get_CAN_Rx_Message(&canMsg);

        xQueuePeek(*xCanRxQueue, &canMsg, pdMS_TO_TICKS(500));

        // Envoyer à la queue appropriée selon l'ID avec switch
        while ((xQueueReceive(*xCanRxQueue, &canMsg, pdMS_TO_TICKS(0))) == pdPASS)
        {
            appMsg.data = canMsg.data;

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
                break;
            case CAN_inverter_tempId:
                appMsg.id = inverter_tempId;
                xQueueSend(*xMainQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_voltageId:
                appMsg.id = voltageId;
                xQueueSend(*xMainQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            default:
                break;
            }
            console_print("sent appMsg id:%d data:%d\n", appMsg.id, appMsg.data);
        }

        // console_print((ledState = !ledState) ? "Led ON\n" : "Led OFF\n");
        appMsg.timestamp = xTaskGetTickCount();
        console_print("------(E)CanRx------\n\n");
    }
}

/*#ifdef LINUX
#else
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    console_print("CAN RX FIFO 0 message pending callback triggered\n");
    BaseType_t xHigherPriorityTaskWoken = pdTRUE;
    xSemaphoreGiveFromISR(xCanRxSemaphore, NULL);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
#endif

void get_CAN_Rx_Message(dataCan *appMsg)
{
#ifdef LINUX
    CAN_ReadNext(appMsg);
#else
    GetNextCANMessage(appMsg, 0, 0);
#endif
}*/
