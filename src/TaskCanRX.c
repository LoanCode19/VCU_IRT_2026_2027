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
    TickType_t lastWakeUp = xTaskGetTickCount();

#ifdef LINUX
    CAN_Replay_Init("./VCU_APP/dataSets/can_messages.csv");
#endif

    CanRxParams_t *params = (CanRxParams_t *)pvParameters;
    QueueHandle_t *xMainQueue = params->xMainQueue;
    QueueHandle_t *xTRVPQueue = params->xTRVPQueue;

    xCanRxSemaphore = xSemaphoreCreateBinary();

    while (1)
    {
        xSemaphoreTake(xCanRxSemaphore, portMAX_DELAY);

        console_print("------(B)CanRx------\n");
        dataMessage_t msg = {0, 0};

        dataCan canMsg = {0};
        get_CAN_Rx_Message(&canMsg);

        msg.data = canMsg.data;

        // Envoyer à la queue appropriée selon l'ID avec switch
        switch ((CanIds)canMsg.id)
        {
        // Wheel speeds went to Main Task
        case CAN_wheelFRId:
            msg.id = wheelFRId;
            xQueueSend(*xTRVPQueue, &msg, pdMS_TO_TICKS(5));
            break;
        case CAN_wheelFLId:
            msg.id = wheelFLId;
            xQueueSend(*xTRVPQueue, &msg, pdMS_TO_TICKS(5));
            break;
        case CAN_wheelRRId:
            msg.id = wheelRRId;
            xQueueSend(*xTRVPQueue, &msg, pdMS_TO_TICKS(5));
            break;
        case CAN_wheelRLId:
            msg.id = wheelRLId;
            xQueueSend(*xTRVPQueue, &msg, pdMS_TO_TICKS(5));
            break;

        case CAN_apps1Id:
            msg.id = apps1Id;
            xQueueSend(*xTRVPQueue, &msg, pdMS_TO_TICKS(5));
            break;
        case CAN_apps2Id:
            msg.id = apps2Id;
            xQueueSend(*xTRVPQueue, &msg, pdMS_TO_TICKS(5));
            break;

        case CAN_motor_tempId:
            msg.id = motor_tempId;
            xQueueSend(*xMainQueue, &msg, pdMS_TO_TICKS(5));
            break;
        case CAN_inverter_tempId:
            msg.id = inverter_tempId;
            xQueueSend(*xMainQueue, &msg, pdMS_TO_TICKS(5));
            break;
        case CAN_voltageId:
            msg.id = voltageId;
            xQueueSend(*xMainQueue, &msg, pdMS_TO_TICKS(5));
            break;
        default:
            break;
        }
        console_print("sent msg id:%d data:%d\n", msg.id, msg.data);

        // console_print((ledState = !ledState) ? "Led ON\n" : "Led OFF\n");
        msg.timestamp = xTaskGetTickCount();
        console_print("------(E)CanRx------\n\n");
    }
}

void get_CAN_Rx_Message(dataCan *msg)
{
#ifdef LINUX
    CAN_ReadNext(msg);
#else
    msg->data = 128;
    msg->id = 12;
#endif
}