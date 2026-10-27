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
#include "project_config.h"

#define WHEELFR_IDX 0
#define WHEELFL_IDX 1
#define WHEELRR_IDX 2
#define WHEELRL_IDX 3

#define WHEEL_COUNT 4
#define APPS_COUNT 2

#define MAX_APPS_TIMEOUT 200
#define MAX_WHEEL_TIMEOUT 400

void printTRVPState(const TRVPState_t *state, float pedalCmd);

// Messages perdus parce que la queue de Main etait pleine (lisible au debogueur). TRVP n'attend pas : sa
// boucle garde sa periode (R1).
static uint32_t trvpSendFailures = 0;

void TaskRawValuesProcessing(void *pvParameters)
{
    TRVPState_t trvpState = {0};
    trvpState.brakePedalPressed = false;
    AppsMonitor_t appsMonitor = {0}; // S1 : incoherence en cours, faute confirmee (D2)

    TRVPParams_t *params = (TRVPParams_t *)pvParameters;
    QueueHandle_t *xTRVPQueue = params->xTRVPQueue;
    QueueHandle_t *xMainQueue = params->xMainQueue;
    TaskId taskId = params->taskId;
    TickType_t lastTrace = xTaskGetTickCount();

    trvpState.wheelSpeeds[WHEELFR_IDX].timeStatus = STATUS_NOT_READY;
    trvpState.wheelSpeeds[WHEELFL_IDX].timeStatus = STATUS_NOT_READY;
    trvpState.wheelSpeeds[WHEELRR_IDX].timeStatus = STATUS_NOT_READY;
    trvpState.wheelSpeeds[WHEELRL_IDX].timeStatus = STATUS_NOT_READY;
    trvpState.appsValues[0].timeStatus = STATUS_NOT_READY;
    trvpState.appsValues[1].timeStatus = STATUS_NOT_READY;

    // R1 (T3) : une passe toutes les CONTROL_PERIOD_MS, quel que soit le trafic CAN
    TickType_t lastWake = xTaskGetTickCount();

    while (1)
    {
        dataMessage_t msg = {0};
        while ((xQueueReceive(*xTRVPQueue, &msg, pdMS_TO_TICKS(0))) == pdPASS)
        {
            msg.timeStatus = STATUS_TIME_OK;

            switch (msg.id)
            {
            case wheelFRId: // motor temp
                trvpState.wheelSpeeds[WHEELFR_IDX] = msg;
                break;
            case wheelFLId: // inverter temp
                trvpState.wheelSpeeds[WHEELFL_IDX] = msg;
                break;
            case wheelRRId: // voltage
                trvpState.wheelSpeeds[WHEELRR_IDX] = msg;
                break;
            case wheelRLId: // calculated speed
                trvpState.wheelSpeeds[WHEELRL_IDX] = msg;
                break;
            case apps1Id: // calculated throttle command
                trvpState.appsValues[0] = msg;
                break;
            case apps2Id: // calculated throttle command
                trvpState.appsValues[1] = msg;
                break;
            }
        }

        updateDataTimeStatus(trvpState.wheelSpeeds, WHEEL_COUNT, MAX_WHEEL_TIMEOUT, xTaskGetTickCount()); // if you need the actual time, call the function directly here!!!
        updateDataTimeStatus(trvpState.appsValues, APPS_COUNT, MAX_APPS_TIMEOUT, xTaskGetTickCount());

        trvpState.speed = calculateSpeed(trvpState.wheelSpeeds, WHEEL_COUNT);

        // MUST give APPS values in the order defined int potmin and potMax in throttle parameters!!!
        float throttleCommand = GetUserThrottleCommand(&trvpState.appsValues[0], &trvpState.appsValues[1], trvpState.speed, trvpState.brakePedalPressed);
        // Faute APPS confirmee : couple coupe jusqu'a des APPS coherents et la pedale relachee (D2)
        if (!AppsMonitorStep(&appsMonitor, AppsImplausible(&trvpState.appsValues[0], &trvpState.appsValues[1]),
                             AppsPedalPercent(&trvpState.appsValues[0]), xTaskGetTickCount()))
        {
            throttleCommand = 0.0f;
        }

        dataMessage_t throttleMsg = {
            .id = processedThrottleId,
            .data = FP_FROMFLT(throttleCommand), // convert to percentage and send as int
            .timestamp = xTaskGetTickCount()};

        dataMessage_t speedMsg = {
            .id = calculatedSpeedId,
            .data = trvpState.speed, // convert to percentage and send as int
            .timestamp = xTaskGetTickCount()};

        if (xQueueSend(*xMainQueue, &throttleMsg, 0) != pdPASS)
        {
            trvpSendFailures++;
        }
        if (xQueueSend(*xMainQueue, &speedMsg, 0) != pdPASS)
        {
            trvpSendFailures++;
        }

        trvpState.lastCallTimeStmp = xTaskGetTickCount();

        // Traces : l'etat une fois par seconde, pas a chaque passage (T17)
        if ((TickType_t)(trvpState.lastCallTimeStmp - lastTrace) >= pdMS_TO_TICKS(TRACE_PERIOD_MS))
        {
            lastTrace = trvpState.lastCallTimeStmp;
            printTRVPState(&trvpState, throttleCommand);
        }

#ifdef USE_WDG
        // feed WDG
        wdgFeed(taskId, xTaskGetTickCount());
#endif

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

void printTRVPState(const TRVPState_t *state, float pedalCmd)
{
    console_print("TRVP State:\n");
    console_print("  Pedal command: %ld (0.1 %%)\n", (long)(pedalCmd * 10.0f));
    console_print("  Brake Pedal Pressed: %s\n", state->brakePedalPressed ? "Yes" : "No");
    console_print("  Speed: %lu\n", (unsigned long)state->speed);
    for (int i = 0; i < WHEEL_COUNT; i++)
    {
        console_print("  Wheel %d Speed: %ld, Time Status: %d\n", i, (long)state->wheelSpeeds[i].data, state->wheelSpeeds[i].timeStatus);
    }
    for (int i = 0; i < APPS_COUNT; i++)
    {
        console_print("  APPS %d Value: %ld, Time Status: %d\n", i, (long)state->appsValues[i].data, state->appsValues[i].timeStatus);
    }
}
