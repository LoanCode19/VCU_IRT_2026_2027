#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "structs.h"
#include "project_config.h"

#include "console.h"
#include "TaskRawValuesProcessing.h"

#include "utils.h"
#include "my_fp.h"

#define WHEEL_COUNT 4
#define APPS_COUNT 2

static void PrintTRVPState(const TRVPState_t *state, float pedalCmd);

// Messages perdus parce que la queue de Main etait pleine (lisible au debogueur). TRVP n'attend pas : sa
// boucle garde sa periode (R1).
static uint32_t trvpSendFailures = 0;

// Range un message recu dans l'etat de TRVP. Les vitesses de roue passent en mm/s des la reception (D7).
static void StoreMessage(TRVPState_t *state, dataMessage_t msg)
{
    msg.timeStatus = STATUS_TIME_OK;
    switch (msg.id)
    {
    case wheelFRId:
    case wheelFLId:
    case wheelRRId:
    case wheelRLId:
        msg.data = WheelRawToMmS(msg.data);
        state->wheelSpeeds[msg.id - wheelFRId] = msg;
        break;
    case apps1Id:
    case apps2Id:
        state->appsValues[msg.id - apps1Id] = msg;
        break;
    case brakePressedId:
        state->brake = msg;
        break;
    default: // message que la tache n'attend pas : ignore
        break;
    }
}

void TaskRawValuesProcessing(void *pvParameters)
{
    TRVPParams_t *params = (TRVPParams_t *)pvParameters;
    QueueHandle_t *xTRVPQueue = params->xTRVPQueue;
    QueueHandle_t *xMainQueue = params->xMainQueue;
    TaskId taskId = params->taskId;

    TRVPState_t trvpState = {0};
    for (int i = 0; i < WHEEL_COUNT; i++)
    {
        trvpState.wheelSpeeds[i].timeStatus = STATUS_NOT_READY;
    }
    for (int i = 0; i < APPS_COUNT; i++)
    {
        trvpState.appsValues[i].timeStatus = STATUS_NOT_READY;
    }
    trvpState.brake.timeStatus = STATUS_NOT_READY;
    AppsMonitor_t appsMonitor = {0}; // S1 : incoherence en cours, faute confirmee (D2)
    TickType_t lastTrace = xTaskGetTickCount();

    // R1 (T3) : une passe toutes les CONTROL_PERIOD_MS, quel que soit le trafic CAN
    TickType_t lastWake = xTaskGetTickCount();

    while (1)
    {
        dataMessage_t msg = {0};
        while (xQueueReceive(*xTRVPQueue, &msg, 0) == pdPASS)
        {
            StoreMessage(&trvpState, msg);
        }

        TickType_t now = xTaskGetTickCount();
        updateDataTimeStatus(trvpState.wheelSpeeds, WHEEL_COUNT, WHEEL_TIMEOUT_MS, now);
        updateDataTimeStatus(trvpState.appsValues, APPS_COUNT, APPS_TIMEOUT_MS, now);
        trvpState.speed = calculateSpeed(trvpState.wheelSpeeds, WHEEL_COUNT);

        // Frein (S2) : il agit sur le couple, c'est un signal critique (T 11.9.1). Jamais recu, perime ou d'une
        // valeur autre que 0 : il compte comme appuye, et le couple est coupe.
        trvpState.brakePedalPressed =
            !dataFresh(&trvpState.brake, VEHICLE_STATE_TIMEOUT_MS, now) || (trvpState.brake.data != 0);

        // APPS1 puis APPS2, dans l'ordre de potmin et potmax (throttle.c)
        const dataMessage_t *apps1 = &trvpState.appsValues[0];
        const dataMessage_t *apps2 = &trvpState.appsValues[1];
        float throttleCommand = GetUserThrottleCommand(apps1, apps2, trvpState.speed, trvpState.brakePedalPressed);
        // Faute APPS confirmee : couple coupe jusqu'a des APPS coherents et la pedale relachee (D2)
        if (!AppsMonitorStep(&appsMonitor, AppsImplausible(apps1, apps2), AppsPedalPercent(apps1), now))
        {
            throttleCommand = 0.0f;
        }

        SendDataMessage(*xMainQueue, processedThrottleId, FP_FROMFLT(throttleCommand), now, &trvpSendFailures);
        SendDataMessage(*xMainQueue, calculatedSpeedId, trvpState.speed, now, &trvpSendFailures); // mm/s
        // Faute APPS confirmee (S1, D2) : Main allume le voyant defaut
        SendDataMessage(*xMainQueue, appsFaultId, appsMonitor.latched ? 1 : 0, now, &trvpSendFailures);

        // Traces : l'etat une fois par seconde, pas a chaque passage (T17)
        if ((TickType_t)(now - lastTrace) >= pdMS_TO_TICKS(TRACE_PERIOD_MS))
        {
            lastTrace = now;
            PrintTRVPState(&trvpState, throttleCommand);
        }

        wdgFeedNow(taskId);

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

static void PrintTRVPState(const TRVPState_t *state, float pedalCmd)
{
    console_print("TRVP State:\n");
    console_print("  Pedal command: %ld (0.1 %%)\n", (long)(pedalCmd * 10.0f));
    console_print("  Brake Pedal Pressed: %s\n", state->brakePedalPressed ? "Yes" : "No");
    console_print("  Speed: %ld mm/s\n", (long)state->speed);
    for (int i = 0; i < WHEEL_COUNT; i++)
    {
        console_print("  Wheel %d Speed: %ld, Time Status: %d\n", i, (long)state->wheelSpeeds[i].data, state->wheelSpeeds[i].timeStatus);
    }
    for (int i = 0; i < APPS_COUNT; i++)
    {
        console_print("  APPS %d Value: %ld, Time Status: %d\n", i, (long)state->appsValues[i].data, state->appsValues[i].timeStatus);
    }
}
