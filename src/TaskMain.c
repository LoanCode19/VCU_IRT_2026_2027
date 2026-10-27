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
#include "vcuState.h"
#include "project_config.h"

void PrintMainState(const MainState_t *mainState, VcuState_t r2dState, float torqueCmd);

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

// T19 : met a jour l'etat des quatre signaux de la trame d'etat du vehicule. true s'ils sont tous arrives a temps.
static bool VehicleStateFresh(MainState_t *state, TickType_t now)
{
    dataMessage_t *signals[4] = {&state->tsActive, &state->sdcClosed, &state->brake, &state->r2dSwitch};
    bool fresh = true;
    for (unsigned i = 0; i < 4U; i++)
    {
        updateDataTimeStatus(signals[i], 1, VEHICLE_STATE_TIMEOUT_MS, now);
        fresh = fresh && (signals[i]->timeStatus == STATUS_TIME_OK);
    }
    return fresh;
}

void TaskMain(void *pvParameters)
{
    MainState_t mainState = {0};
    bool faultLightOn = false; // dernier ordre FaultLight envoye a l'IHM
    bool buzzerOn = false;     // dernier ordre R2Dbuzzer envoye a l'IHM
    VcuStateMachine_t r2d;     // Ready-to-Drive (S3, T8)
    VcuStateInit(&r2d);
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
    mainState.tsActive.timeStatus = STATUS_NOT_READY;
    mainState.sdcClosed.timeStatus = STATUS_NOT_READY;
    mainState.brake.timeStatus = STATUS_NOT_READY;
    mainState.r2dSwitch.timeStatus = STATUS_NOT_READY;

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
            case tsActiveId:
                mainState.tsActive = msg;
                break;
            case sdcClosedId:
                mainState.sdcClosed = msg;
                break;
            case brakePressedId:
                mainState.brake = msg;
                break;
            case r2dSwitchId:
                mainState.r2dSwitch = msg;
                break;
            }
        }

        // Ready-to-Drive (S3, T8) : la pedale ne donne du couple qu'en R2D
        VcuInputs_t vehicle = VcuInputsFromSignals(VehicleStateFresh(&mainState, now), mainState.tsActive.data,
                                                   mainState.sdcClosed.data, mainState.brake.data,
                                                   mainState.r2dSwitch.data);
        VcuOutputs_t r2dOut = VcuStateStep(&r2d, &vehicle, now);

        // S5 (T1, T2) : une donnee absente ou perimee coupe le couple, temperatures comprises (T1). Une
        // consigne 0 part quand meme (l'onduleur doit recevoir 0, pas un silence), et le voyant defaut
        // s'allume, comme pour une trame d'etat du vehicule perdue (T19). Hors R2D ou donnee perimee, la
        // demande passe a 0 dans le calcul : la rampe repartira de 0.
        bool inputsValid = MainInputsValid(&mainState, now);
        bool fault = !inputsValid || !vehicle.fresh;
        if (fault != faultLightOn)
        {
            IHMOrder_t faultOrder = {.order = FaultLight, .state = fault};
            if (xQueueSend(*xIHMQueue, &faultOrder, 0) == pdPASS)
            {
                faultLightOn = fault;
            }
        }

        // Son R2D (EV 4.12.1) : un ordre a l'IHM a chaque changement
        if (r2dOut.buzzerOn != buzzerOn)
        {
            IHMOrder_t buzzerOrder = {.order = R2Dbuzzer, .state = r2dOut.buzzerOn};
            if (xQueueSend(*xIHMQueue, &buzzerOrder, 0) == pdPASS)
            {
                buzzerOn = r2dOut.buzzerOn;
            }
        }

        // Une consigne a chaque periode, meme sans message (R1). La rampe avance du temps reellement ecoule.
        bool torqueAllowed = inputsValid && r2dOut.torqueAllowed;
        float request = torqueAllowed ? FP_TOFLOAT(mainState.ThrottleCommand.data) : 0.0f;
        uint32_t elapsedMs = (uint32_t)((now - lastPass) * portTICK_PERIOD_MS);
        lastPass = now;
        float speedMs = (float)mainState.speed.data / 1000.0f; // mm/s dans les messages, m/s dans le calcul (D7)
        float throttle = ProcessThrottle(request, speedMs, mainState.motorTemp.data, mainState.inverterTemp.data,
                                         mainState.Voltage.data, elapsedMs);
        if (!torqueAllowed)
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

        // "Drive enable" du DTI a chaque periode (T20) : deuxieme barriere, dans l'onduleur
        dataMessage_t enableMsg = {.id = driveEnableCmdId, .data = r2dOut.driveEnable ? 1 : 0, .timestamp = now};
        if (xQueueSend(*xCanTxQueue, &enableMsg, 0) != pdPASS)
        {
            canTxSendFailures++;
        }

        // Traces : l'etat une fois par seconde, pas a chaque passage (T17)
        if ((TickType_t)(now - lastTrace) >= pdMS_TO_TICKS(TRACE_PERIOD_MS))
        {
            lastTrace = now;
            PrintMainState(&mainState, r2d.state, torqueCmd);
        }

#ifdef USE_WDG
        // feed WDG
        wdgFeed(taskId, xTaskGetTickCount());
#endif

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

// function, to print current state of the motor controller (for debug)
void PrintMainState(const MainState_t *mainState, VcuState_t r2dState, float torqueCmd)
{
    console_print("Main State:\n");
    console_print("  R2D state: %d (0 TS off, 1 TS on, 2 R2D)\n", (int)r2dState);
    console_print("  Torque command: %ld (0.1 %%)\n", (long)(torqueCmd * 10.0f));
    console_print("  Throttle Command: %ld state:%d\n ", (long)mainState->ThrottleCommand.data, mainState->ThrottleCommand.timeStatus);
    console_print("  Speed: %ld state:%d\n", (long)mainState->speed.data, mainState->speed.timeStatus);
    console_print("  TS: %ld SDC: %ld Brake: %ld R2D switch: %ld state:%d\n", (long)mainState->tsActive.data,
                  (long)mainState->sdcClosed.data, (long)mainState->brake.data, (long)mainState->r2dSwitch.data,
                  mainState->r2dSwitch.timeStatus);
    console_print("  Motor Temp: %ld state:%d\n", (long)mainState->motorTemp.data, mainState->motorTemp.timeStatus);
    console_print("  Inverter Temp: %ld state:%d\n", (long)mainState->inverterTemp.data, mainState->inverterTemp.timeStatus);
    console_print("  Voltage: %ld state:%d\n", (long)mainState->Voltage.data, mainState->Voltage.timeStatus);
}