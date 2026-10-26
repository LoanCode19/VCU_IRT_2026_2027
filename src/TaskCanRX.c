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
volatile CanRxStats_t canRxStats = {0};

// Table de decodage (lot 4, valeurs du 26/10/2026) : une ligne par signal. Une trame peut porter
// plusieurs signaux (temperatures du DTI). Seules les trames standard (ID sur 11 bits) sont lues.
const CanRxSignal_t canRxTable[] = {
    // Capteurs, format provisoire jusqu'au DBC (D6) : une valeur de 4 octets en tete de trame
    {CAN_ID_WHEEL_FR, 0U, 4U, true, 1, wheelFRId, CAN_TO_TRVP},
    {CAN_ID_WHEEL_FL, 0U, 4U, true, 1, wheelFLId, CAN_TO_TRVP},
    {CAN_ID_WHEEL_RR, 0U, 4U, true, 1, wheelRRId, CAN_TO_TRVP},
    {CAN_ID_WHEEL_RL, 0U, 4U, true, 1, wheelRLId, CAN_TO_TRVP},
    {CAN_ID_APPS1, 0U, 4U, true, 1, apps1Id, CAN_TO_TRVP},
    {CAN_ID_APPS2, 0U, 4U, true, 1, apps2Id, CAN_TO_TRVP},
    // Onduleur DTI HV-550 (DTI CAN manual V2.5) : temperatures en 0,1 degre, tension en volts
    {CAN_ID_DTI_TEMPERATURES, 0U, 2U, true, 10, inverter_tempId, CAN_TO_MAIN_AND_COOLING},
    {CAN_ID_DTI_TEMPERATURES, 2U, 2U, true, 10, motor_tempId, CAN_TO_MAIN_AND_COOLING},
    {CAN_ID_DTI_ERPM_VOLTAGE, 6U, 2U, true, 1, voltageId, CAN_TO_MAIN},
};
const unsigned canRxTableSize = sizeof(canRxTable) / sizeof(canRxTable[0]);

// Le refroidissement recoit aussi les temperatures. Pointeur NULL : pas de refroidissement
// (tests qui remplissent CanRxParams_t sans ce champ).
static void SendToCooling(QueueHandle_t *xCoolingQueue, const dataMessage_t *msg)
{
    if ((xCoolingQueue != NULL) && (*xCoolingQueue != NULL))
    {
        xQueueSend(*xCoolingQueue, msg, pdMS_TO_TICKS(5));
    }
}

// Extrait un signal d'apres sa ligne de table. Renvoie false si la trame est trop courte.
static bool DecodeSignal(const CanFrame_t *frame, const CanRxSignal_t *signal, int32_t *value)
{
    if (frame->dlc < (uint8_t)(signal->startByte + signal->length))
    {
        return false;
    }

    uint32_t raw = 0U;
    for (uint8_t i = 0U; i < signal->length; i++)
    {
        raw = (raw << 8) | frame->data[signal->startByte + i]; // big-endian
    }

    int32_t decoded = (int32_t)raw;
    if (signal->isSigned && (signal->length < 4U))
    {
        uint32_t signBit = 1UL << ((8U * signal->length) - 1U);
        decoded = (int32_t)(raw ^ signBit) - (int32_t)signBit; // extension de signe
    }
    if (signal->divisor > 1)
    {
        // Arrondi au plus proche, la moitie s'eloignant de zero : 45,5 -> 46, -45,5 -> -46
        decoded = (decoded >= 0) ? ((decoded + (signal->divisor / 2)) / signal->divisor)
                                 : ((decoded - (signal->divisor / 2)) / signal->divisor);
    }
    *value = decoded;
    return true;
}

static void RouteMessage(const CanRxParams_t *params, CanRoute_t route, const dataMessage_t *msg)
{
    if (route == CAN_TO_TRVP)
    {
        xQueueSend(*params->xTRVPQueue, msg, pdMS_TO_TICKS(5));
        return;
    }
    xQueueSend(*params->xMainQueue, msg, pdMS_TO_TICKS(5));
    if (route == CAN_TO_MAIN_AND_COOLING)
    {
        SendToCooling(params->xCoolingQueue, msg);
    }
}

// Applique toutes les lignes de la table qui portent l'ID de la trame. Les rejets sont comptes.
static void ProcessFrame(const CanRxParams_t *params, const CanFrame_t *frame)
{
    bool known = false;
    bool tooShort = false;

    if (frame->ide == 0U) // trames standard seulement
    {
        for (unsigned i = 0; i < canRxTableSize; i++)
        {
            const CanRxSignal_t *signal = &canRxTable[i];
            if (signal->id != frame->id)
            {
                continue;
            }
            known = true;

            dataMessage_t appMsg = {.id = signal->appId, .timestamp = frame->rxTick}; // heure de reception (B1)
            if (!DecodeSignal(frame, signal, &appMsg.data))
            {
                tooShort = true;
                continue;
            }
            RouteMessage(params, signal->route, &appMsg);
            console_print("sent appMsg id:%d data:%ld\n", appMsg.id, (long)appMsg.data);
        }
    }

    if (!known)
    {
        canRxStats.unknownId++;
    }
    else if (tooShort)
    {
        canRxStats.badDlc++;
    }
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
    QueueHandle_t *xCanRxQueue = params->xCanRxQueue;
    QueueHandle_t *xWDGQueue = params->xWDGQueue;
    TaskId taskId = params->taskId;

    TickType_t timeNow = xTaskGetTickCount();

    while (1)
    {
        CanFrame_t frame = {0};

        xQueuePeek(*xCanRxQueue, &frame, CAN_RX_TIMEOUT);
        console_print("------(B)CanRx------\n");

        while ((xQueueReceive(*xCanRxQueue, &frame, pdMS_TO_TICKS(0))) == pdPASS)
        {
            ProcessFrame(params, &frame);
        }

        // console_print((ledState = !ledState) ? "Led ON\n" : "Led OFF\n");

#ifdef USE_WDG
        // feed WDG
        wdgFeed(taskId, xTaskGetTickCount(), xWDGQueue);
#endif

        console_print("------(E)CanRx------\n\n");
    }
}
