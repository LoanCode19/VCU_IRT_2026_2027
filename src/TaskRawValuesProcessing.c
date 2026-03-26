#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"

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

void printTRVPState(const TRVPState_t *state);

void TaskRawValuesProcessing(void *pvParameters)
{
    TRVPState_t trvpState = {0};
    trvpState.brakePedalPressed = false;

    TRVPParams_t *params = (TRVPParams_t *)pvParameters;
    QueueHandle_t *xTRVPQueue = params->xTRVPQueue;
    QueueHandle_t *xMainQueue = params->xMainQueue;

    trvpState.wheelSpeeds[WHEELFR_IDX].timeStatus = STATUS_NOT_READY;
    trvpState.wheelSpeeds[WHEELFL_IDX].timeStatus = STATUS_NOT_READY;
    trvpState.wheelSpeeds[WHEELRR_IDX].timeStatus = STATUS_NOT_READY;
    trvpState.wheelSpeeds[WHEELRL_IDX].timeStatus = STATUS_NOT_READY;
    trvpState.appsValues[0].timeStatus = STATUS_NOT_READY;
    trvpState.appsValues[1].timeStatus = STATUS_NOT_READY;

    while (1)
    {
        TickType_t timeNow = xTaskGetTickCount();
        dataMessage_t msg = {0};
        xQueuePeek(*xTRVPQueue, &msg, pdMS_TO_TICKS(500));

        console_print("------(B)TRVP------\n");

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

            console_print("trvp:Received from queue: id=%lu, timestamp=%lu\n", msg.id, msg.timestamp);
        }

        updateDataTimeStatus(trvpState.wheelSpeeds, WHEEL_COUNT, MAX_WHEEL_TIMEOUT, xTaskGetTickCount()); // if you need the actual time, call the function directly here!!!
        updateDataTimeStatus(trvpState.appsValues, APPS_COUNT, MAX_APPS_TIMEOUT, xTaskGetTickCount());

        trvpState.speed = calculateSpeed(trvpState.wheelSpeeds, WHEEL_COUNT);
        console_print("trvp:Calculated speed: %.2f\n", trvpState.speed);

        // MUST give APPS values in the order defined int potmin and potMax in throttle parameters!!!
        float throttleCommand = GetUserThrottleCommand(&trvpState.appsValues[0], &trvpState.appsValues[1], trvpState.speed, trvpState.brakePedalPressed);
        console_print("trvp:Calculated throttle command: %.2f\n", throttleCommand);

        dataMessage_t throttleMsg = {
            .id = throttleCmdId,
            .data = FP_FROMFLT(throttleCommand), // convert to percentage and send as int
            .timestamp = xTaskGetTickCount()};

        dataMessage_t speedMsg = {
            .id = calculatedSpeedId,
            .data = trvpState.speed, // convert to percentage and send as int
            .timestamp = xTaskGetTickCount()};

        xQueueSend(*xMainQueue, &throttleMsg, pdMS_TO_TICKS(100));
        xQueueSend(*xMainQueue, &speedMsg, pdMS_TO_TICKS(100));

        trvpState.lastCallTimeStmp = xTaskGetTickCount();

        printTRVPState(&trvpState);
        console_print("------(E)TRVP------\n\n");
    }
}

void printTRVPState(const TRVPState_t *state)
{
    console_print("TRVP State:\n");
    console_print("  Brake Pedal Pressed: %s\n", state->brakePedalPressed ? "Yes" : "No");
    console_print("  Speed: %d\n", state->speed);
    for (int i = 0; i < WHEEL_COUNT; i++)
    {
        console_print("  Wheel %d Speed: %d, Time Status: %d\n", i, state->wheelSpeeds[i].data, state->wheelSpeeds[i].timeStatus);
    }
    for (int i = 0; i < APPS_COUNT; i++)
    {
        console_print("  APPS %d Value: %d, Time Status: %d\n", i, state->appsValues[i].data, state->appsValues[i].timeStatus);
    }
}
