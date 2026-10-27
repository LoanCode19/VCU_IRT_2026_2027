#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "structs.h"
#include "project_config.h"

#include "console.h"
#include "TaskMain.h"

#include "utils.h"
#include "my_fp.h"
#include "project_config.h"

void PrintMainState(const MainState_t *mainState, float torqueCmd);

// Consignes perdues parce que la queue de CanTx etait pleine (lisible au debogueur)
static uint32_t canTxSendFailures = 0;

// S5 (T2) : met a jour l'etat de chaque donnee selon son delai. true si toutes sont fraiches.
static bool MainInputsValid(MainState_t *state, TickType_t now)
{
    updateDataTimeStatus(&state->ThrottleCommand, 1, THROTTLE_CMD_TIMEOUT_MS, now);
    updateDataTimeStatus(&state->speed, 1, SPEED_TIMEOUT_MS, now);
    updateDataTimeStatus(&state->motorTemp, 1, TEMP_TIMEOUT_MS, now);
    updateDataTimeStatus(&state->inverterTemp, 1, TEMP_TIMEOUT_MS, now);
    updateDataTimeStatus(&state->Voltage, 1, VOLTAGE_TIMEOUT_MS, now);
    return (state->ThrottleCommand.timeStatus == STATUS_TIME_OK) && (state->speed.timeStatus == STATUS_TIME_OK) &&
           (state->motorTemp.timeStatus == STATUS_TIME_OK) && (state->inverterTemp.timeStatus == STATUS_TIME_OK) &&
           (state->Voltage.timeStatus == STATUS_TIME_OK);
}

void TaskMain(void *pvParameters)
{
    MainState_t mainState = {0};
    bool faultLightOn = false; // dernier ordre FaultLight envoye a l'IHM
    float torqueCmd = 0.0f;    // derniere consigne envoyee, pour les traces

    TickType_t lastTrace = xTaskGetTickCount();

    MainParams_t *params = (MainParams_t *)pvParameters;
    QueueHandle_t *xMainQueue = params->xMainQueue;
    QueueHandle_t *xTRVPQueu = params->xTRVPQueue;
    QueueHandle_t *xIHMQueue = params->xIHMQueue;
    QueueHandle_t *xCanTxQueue = params->xCanTxQueue;
    TaskId taskId = params->taskId;

    mainState.inverterTemp.timeStatus = STATUS_NOT_READY;
    mainState.motorTemp.timeStatus = STATUS_NOT_READY;
    mainState.Voltage.timeStatus = STATUS_NOT_READY;
    mainState.speed.timeStatus = STATUS_NOT_READY;
    mainState.ThrottleCommand.timeStatus = STATUS_NOT_READY;

    // R1 (T3) : une passe toutes les CONTROL_PERIOD_MS, quel que soit le trafic CAN. La premiere passe
    // compte pour une periode : la rampe avance des le premier passage.
    TickType_t lastWake = xTaskGetTickCount();
    TickType_t lastPass = lastWake - pdMS_TO_TICKS(CONTROL_PERIOD_MS);

    while (1)
    {
        TickType_t now = xTaskGetTickCount();
        dataMessage_t msg = {0};
        while ((xQueueReceive(*xMainQueue, &msg, pdMS_TO_TICKS(0))) == pdPASS)
        {
            msg.timeStatus = STATUS_TIME_OK;
            switch (msg.id)
            {
            case motor_tempId: // motor temp
                mainState.motorTemp = msg;
                break;
            case inverter_tempId: // inverter temp
                mainState.inverterTemp = msg;
                break;
            case voltageId: // voltage message
                mainState.Voltage = msg;
                break;
            case calculatedSpeedId: // calculated speed
                mainState.speed = msg;
                break;
            case processedThrottleId: // processed throttle value
                mainState.ThrottleCommand = msg;
                break;
            }
        }

        // S5 (T1, T2) : une donnee absente ou perimee coupe le couple, temperatures comprises (T1). Une
        // consigne 0 part quand meme (l'onduleur doit recevoir 0, pas un silence), et le voyant defaut
        // s'allume. La demande passe a 0 dans le calcul : la rampe repartira de 0 au retour des donnees.
        bool inputsValid = MainInputsValid(&mainState, now);
        if (inputsValid == faultLightOn)
        {
            IHMOrder_t faultOrder = {.order = FaultLight, .state = !inputsValid};
            if (xQueueSend(*xIHMQueue, &faultOrder, 0) == pdPASS)
            {
                faultLightOn = !inputsValid;
            }
        }

        // Une consigne a chaque periode, meme sans message (R1). La rampe avance du temps reellement ecoule.
        float request = inputsValid ? FP_TOFLOAT(mainState.ThrottleCommand.data) : 0.0f;
        uint32_t elapsedMs = (uint32_t)((now - lastPass) * portTICK_PERIOD_MS);
        lastPass = now;
        float throttle = ProcessThrottle(request, mainState.speed.data, mainState.motorTemp.data,
                                         mainState.inverterTemp.data, mainState.Voltage.data, elapsedMs);
        if (!inputsValid)
        {
            throttle = 0.0f;
        }
        dataMessage_t throttleMsg = {
            .id = inverterTorqueCmdId,    // Throttle command message ID
            .data = FP_FROMFLT(throttle), // Convert to percentage and scale
            .timestamp = now};

        if (xQueueSend(*xCanTxQueue, &throttleMsg, 0) != pdPASS)
        {
            canTxSendFailures++; // queue pleine : consigne perdue, mais Main continue (R3)
        }
        torqueCmd = throttle;

        // Traces : l'etat une fois par seconde, pas a chaque passage (T17)
        if ((TickType_t)(now - lastTrace) >= pdMS_TO_TICKS(TRACE_PERIOD_MS))
        {
            lastTrace = now;
            PrintMainState(&mainState, torqueCmd);
        }

#ifdef USE_WDG
        // feed WDG
        wdgFeed(taskId, xTaskGetTickCount());
#endif

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

// function, to print current state of the motor controller (for debug)
void PrintMainState(const MainState_t *mainState, float torqueCmd)
{
    console_print("Main State:\n");
    console_print("  Torque command: %ld (0.1 %%)\n", (long)(torqueCmd * 10.0f));
    console_print("  Throttle Command: %ld state:%d\n ", (long)mainState->ThrottleCommand.data, mainState->ThrottleCommand.timeStatus);
    console_print("  Speed: %ld state:%d\n", (long)mainState->speed.data, mainState->speed.timeStatus);
    console_print("  Brake Pedal Pressed: %s\n", mainState->brakePedalPressed ? "Yes" : "No");
    console_print("  Motor Temp: %ld state:%d\n", (long)mainState->motorTemp.data, mainState->motorTemp.timeStatus);
    console_print("  Inverter Temp: %ld state:%d\n", (long)mainState->inverterTemp.data, mainState->inverterTemp.timeStatus);
    console_print("  Voltage: %ld state:%d\n", (long)mainState->Voltage.data, mainState->Voltage.timeStatus);
}