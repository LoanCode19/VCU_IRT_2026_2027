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

static void PrintMainState(const MainState_t *mainState, VcuState_t r2dState, float torqueCmd);

// Consignes perdues parce que la queue de CanTx etait pleine (lisible au debogueur)
static uint32_t canTxSendFailures = 0;

// Case de MainState_t ou ranger un message recu, ou NULL si Main ne l'attend pas
static dataMessage_t *MainSlot(MainState_t *state, MessageIds id)
{
    switch (id)
    {
    case motor_tempId:
        return &state->motorTemp;
    case inverter_tempId:
        return &state->inverterTemp;
    case voltageId:
        return &state->Voltage;
    case calculatedSpeedId:
        return &state->speed;
    case processedThrottleId:
        return &state->ThrottleCommand;
    case tsActiveId:
        return &state->tsActive;
    case sdcClosedId:
        return &state->sdcClosed;
    case brakePressedId:
        return &state->brake;
    case r2dSwitchId:
        return &state->r2dSwitch;
    case appsFaultId:
        return &state->appsFault;
    default: // message que la tache n'attend pas : ignore
        return NULL;
    }
}

// S5 (T2) : met a jour l'etat de chaque donnee selon son delai. true si toutes sont fraiches.
static bool MainInputsValid(MainState_t *state, TickType_t now)
{
    struct
    {
        dataMessage_t *data;
        int timeoutMs;
    } inputs[] = {
        {&state->ThrottleCommand, THROTTLE_CMD_TIMEOUT_MS},
        {&state->speed, SPEED_TIMEOUT_MS},
        {&state->motorTemp, TEMP_TIMEOUT_MS},
        {&state->inverterTemp, TEMP_TIMEOUT_MS},
        {&state->Voltage, VOLTAGE_TIMEOUT_MS},
    };
    bool valid = true;
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++)
    {
        valid = dataFresh(inputs[i].data, inputs[i].timeoutMs, now) && valid; // chaque donnee est mise a jour
    }
    return valid;
}

// T19 : met a jour l'etat des quatre signaux de la trame d'etat du vehicule. true s'ils sont tous arrives a temps.
static bool VehicleStateFresh(MainState_t *state, TickType_t now)
{
    dataMessage_t *signals[] = {&state->tsActive, &state->sdcClosed, &state->brake, &state->r2dSwitch};
    bool fresh = true;
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
    {
        fresh = dataFresh(signals[i], VEHICLE_STATE_TIMEOUT_MS, now) && fresh; // chaque signal est mis a jour
    }
    return fresh;
}

// Envoie un ordre a l'IHM quand l'etat voulu change. *sent : dernier etat envoye. Queue pleine : l'ordre repart a la
// periode suivante.
static void SendIhmOrderOnChange(QueueHandle_t ihmQueue, Order_t order, bool wanted, bool *sent)
{
    if (wanted != *sent)
    {
        IHMOrder_t ihmOrder = {.order = order, .state = wanted};
        if (xQueueSend(ihmQueue, &ihmOrder, 0) == pdPASS)
        {
            *sent = wanted;
        }
    }
}

void TaskMain(void *pvParameters)
{
    MainParams_t *params = (MainParams_t *)pvParameters;
    QueueHandle_t *xMainQueue = params->xMainQueue;
    QueueHandle_t *xIHMQueue = params->xIHMQueue;
    QueueHandle_t *xCanTxQueue = params->xCanTxQueue;
    TaskId taskId = params->taskId;

    MainState_t mainState = {0};
    mainState.inverterTemp.timeStatus = STATUS_NOT_READY;
    mainState.motorTemp.timeStatus = STATUS_NOT_READY;
    mainState.Voltage.timeStatus = STATUS_NOT_READY;
    mainState.speed.timeStatus = STATUS_NOT_READY;
    mainState.ThrottleCommand.timeStatus = STATUS_NOT_READY;
    mainState.tsActive.timeStatus = STATUS_NOT_READY;
    mainState.sdcClosed.timeStatus = STATUS_NOT_READY;
    mainState.brake.timeStatus = STATUS_NOT_READY;
    mainState.r2dSwitch.timeStatus = STATUS_NOT_READY;

    VcuStateMachine_t r2d; // Ready-to-Drive (S3, T8)
    VcuStateInit(&r2d);
    bool faultLightOn = false; // dernier ordre FaultLight envoye a l'IHM
    bool buzzerOn = false;     // dernier ordre R2Dbuzzer envoye a l'IHM
    TickType_t lastTrace = xTaskGetTickCount();

    // R1 (T3) : une passe toutes les CONTROL_PERIOD_MS, quel que soit le trafic CAN. La premiere passe
    // compte pour une periode : la rampe avance des le premier passage.
    TickType_t lastWake = xTaskGetTickCount();
    TickType_t lastPass = lastWake - pdMS_TO_TICKS(CONTROL_PERIOD_MS);

    while (1)
    {
        TickType_t now = xTaskGetTickCount();
        dataMessage_t msg = {0};
        while (xQueueReceive(*xMainQueue, &msg, 0) == pdPASS)
        {
            dataMessage_t *slot = MainSlot(&mainState, msg.id);
            if (slot != NULL)
            {
                msg.timeStatus = STATUS_TIME_OK;
                *slot = msg;
            }
        }

        // Ready-to-Drive (S3, T8) : la pedale ne donne du couple qu'en R2D
        VcuInputs_t vehicle = VcuInputsFromSignals(VehicleStateFresh(&mainState, now), mainState.tsActive.data,
                                                   mainState.sdcClosed.data, mainState.brake.data,
                                                   mainState.r2dSwitch.data);
        VcuOutputs_t r2dOut = VcuStateStep(&r2d, &vehicle, now);

        // S5 (T1, T2) : une donnee absente ou perimee coupe le couple, temperatures comprises (T1). Une
        // consigne 0 part quand meme (l'onduleur doit recevoir 0, pas un silence), et le voyant defaut
        // s'allume, comme pour une trame d'etat du vehicule perdue (T19) ou une faute APPS confirmee (S1). Hors
        // R2D ou donnee perimee, la demande passe a 0 dans le calcul : la rampe repartira de 0.
        bool inputsValid = MainInputsValid(&mainState, now);
        bool appsFault = (mainState.appsFault.timeStatus == STATUS_TIME_OK) && (mainState.appsFault.data != 0);
        SendIhmOrderOnChange(*xIHMQueue, FaultLight, !inputsValid || !vehicle.fresh || appsFault, &faultLightOn);
        SendIhmOrderOnChange(*xIHMQueue, R2Dbuzzer, r2dOut.buzzerOn, &buzzerOn); // son R2D (EV 4.12.1)

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
        // Queue de CanTx pleine : la consigne est perdue et comptee, mais Main continue (R3)
        SendDataMessage(*xCanTxQueue, inverterTorqueCmdId, FP_FROMFLT(throttle), now, &canTxSendFailures);
        // "Drive enable" du DTI a chaque periode (T20) : deuxieme barriere, dans l'onduleur
        SendDataMessage(*xCanTxQueue, driveEnableCmdId, r2dOut.driveEnable ? 1 : 0, now, &canTxSendFailures);

        // Traces : l'etat une fois par seconde, pas a chaque passage (T17)
        if ((TickType_t)(now - lastTrace) >= pdMS_TO_TICKS(TRACE_PERIOD_MS))
        {
            lastTrace = now;
            PrintMainState(&mainState, r2d.state, throttle);
        }

        wdgFeedNow(taskId);

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

// Etat de Main, pour les traces
static void PrintMainState(const MainState_t *mainState, VcuState_t r2dState, float torqueCmd)
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
