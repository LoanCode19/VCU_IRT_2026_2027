#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"
#include "canIds.h"
#include "queueHandles.h"
#include "utils.h"
#include "project_config.h"

#include "console.h"
#include "TaskCanRX.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <semphr.h>

SemaphoreHandle_t xCanRxSemaphore;

// Le refroidissement recoit aussi les temperatures. Pointeur NULL : pas de refroidissement
// (tests qui remplissent CanRxParams_t sans ce champ).
static void SendToCooling(QueueHandle_t *xCoolingQueue, const dataMessage_t *msg)
{
    if ((xCoolingQueue != NULL) && (*xCoolingQueue != NULL))
    {
        xQueueSend(*xCoolingQueue, msg, pdMS_TO_TICKS(5));
    }
}

// Les trames portent leurs valeurs en big-endian (format provisoire : 4 octets en tete)
static uint32_t ReadBigEndian32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

// Appelee par l'ISR de reception de la carte (et par l'emulateur) : depose la trame pour TaskCanRx.
// Contexte d'interruption : ni attente ni printf.
void CanRx_OnFrameFromISR(const CanFrame_t *frame, BaseType_t *higherPriorityTaskWoken)
{
    if (xCanRxQueue == NULL)
    {
        return; // queues pas encore creees par MainApp()
    }
    (void)xQueueSendFromISR(xCanRxQueue, frame, higherPriorityTaskWoken);
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
        CanFrame_t frame = {0};

        xQueuePeek(*xCanRxQueue, &frame, CAN_RX_TIMEOUT);
        console_print("------(B)CanRx------\n");

        // Envoyer à la queue appropriée selon l'ID avec switch
        while ((xQueueReceive(*xCanRxQueue, &frame, pdMS_TO_TICKS(0))) == pdPASS)
        {
            appMsg.data = (int32_t)ReadBigEndian32(frame.data);
            appMsg.timestamp = frame.rxTick; // heure de reception (B1)

            switch (frame.id)
            {
            // Wheel speeds went to Main Task
            case CAN_ID_WHEEL_FR:
                appMsg.id = wheelFRId;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_ID_WHEEL_FL:
                appMsg.id = wheelFLId;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_ID_WHEEL_RR:
                appMsg.id = wheelRRId;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_ID_WHEEL_RL:
                appMsg.id = wheelRLId;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;

            case CAN_ID_APPS1:
                appMsg.id = apps1Id;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;
            case CAN_ID_APPS2:
                appMsg.id = apps2Id;
                xQueueSend(*xTRVPQueue, &appMsg, pdMS_TO_TICKS(5));
                break;

            case CAN_ID_MOTOR_TEMP:
                appMsg.id = motor_tempId;
                xQueueSend(*xMainQueue, &appMsg, pdMS_TO_TICKS(5));
                SendToCooling(xCoolingQueue, &appMsg);
                break;
            case CAN_ID_INVERTER_TEMP:
                appMsg.id = inverter_tempId;
                xQueueSend(*xMainQueue, &appMsg, pdMS_TO_TICKS(5));
                SendToCooling(xCoolingQueue, &appMsg);
                break;
            case CAN_ID_VOLTAGE:
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
