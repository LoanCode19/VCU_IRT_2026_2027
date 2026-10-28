#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "project_config.h"
#include "queueHandles.h"
#include "my_math.h"
#include "utils.h"
#include "TaskCoolingSystem.h"

typedef struct
{
    dataMessage_t motorTemp;
    dataMessage_t inverterTemp;
    dataMessage_t tsActive; // trame d'etat du vehicule (T19)
    bool pumpByTemp;        // pompe demandee par les temperatures (avec hysteresis)
    bool fansByTemp;        // ventilateurs demandes par les temperatures (avec hysteresis)
    bool published;         // les premiers ordres sont partis
    bool pumpOn;            // derniers ordres envoyes a TaskDigitalSignals
    bool fansOn;
} CoolingState_t;

// Case de CoolingState_t ou ranger un message recu, ou NULL si la tache ne l'attend pas
static dataMessage_t *CoolingSlot(CoolingState_t *state, MessageIds id)
{
    switch (id)
    {
    case motor_tempId:
        return &state->motorTemp;
    case inverter_tempId:
        return &state->inverterTemp;
    case tsActiveId:
        return &state->tsActive;
    default: // message que la tache n'attend pas : ignore
        return NULL;
    }
}

static bool Hysteresis(bool on, int32_t temp, int32_t onTemp, int32_t offTemp)
{
    return on ? (temp > offTemp) : (temp >= onTemp);
}

// S12 (T9, decide le 27/10/2026, revisable) : TS actif -> pompe et ventilateurs ON, quelle que soit la temperature
// (test de pluie, IN 9.2.1). Signal TS perdu -> ON, comme une temperature perdue. Jamais recu : rien de force.
static bool TsForcesCooling(const CoolingState_t *state)
{
    return (state->tsActive.timeStatus == STATUS_TIMEOUT) ||
           ((state->tsActive.timeStatus == STATUS_TIME_OK) && (state->tsActive.data != 0));
}

// Demande des temperatures : la plus chaude des temperatures valides, avec hysteresis. Aucune valide : ON si l'une
// d'elles est perdue (delai depasse), OFF si aucune n'est encore arrivee.
static void EvaluateTemperatures(CoolingState_t *state)
{
    bool motorValid = (state->motorTemp.timeStatus == STATUS_TIME_OK);
    bool inverterValid = (state->inverterTemp.timeStatus == STATUS_TIME_OK);

    if (!motorValid && !inverterValid)
    {
        bool lost = (state->motorTemp.timeStatus == STATUS_TIMEOUT) ||
                    (state->inverterTemp.timeStatus == STATUS_TIMEOUT);
        state->pumpByTemp = lost;
        state->fansByTemp = lost;
        return;
    }

    int32_t hottest;
    if (motorValid && inverterValid)
    {
        hottest = MAX(state->motorTemp.data, state->inverterTemp.data);
    }
    else
    {
        hottest = motorValid ? state->motorTemp.data : state->inverterTemp.data;
    }
    state->pumpByTemp = Hysteresis(state->pumpByTemp, hottest, COOLING_PUMP_ON_TEMP_C, COOLING_PUMP_OFF_TEMP_C);
    state->fansByTemp = Hysteresis(state->fansByTemp, hottest, COOLING_FANS_ON_TEMP_C, COOLING_FANS_OFF_TEMP_C);
}

static void PublishOrder(Order_t order, bool on)
{
    if (xDSQueue != NULL)
    {
        DigitalOrder_t outputOrder = {.order = order, .state = on};
        (void)xQueueSend(xDSQueue, &outputOrder, DS_SEND_TIMEOUT);
    }
}

// Envoie a TaskDigitalSignals les ordres qui changent, et les deux la premiere fois
static void PublishOrders(CoolingState_t *state)
{
    bool tsForces = TsForcesCooling(state);
    bool pumpOn = state->pumpByTemp || tsForces;
    bool fansOn = state->fansByTemp || tsForces;

    if (!state->published || (pumpOn != state->pumpOn))
    {
        PublishOrder(CoolingPump, pumpOn);
    }
    if (!state->published || (fansOn != state->fansOn))
    {
        PublishOrder(CoolingFans, fansOn);
    }
    state->published = true;
    state->pumpOn = pumpOn;
    state->fansOn = fansOn;
}

void TaskCoolingSystem(void *pvParameters)
{
    CoolingParams_t *params = (CoolingParams_t *)pvParameters;
    QueueHandle_t *xTCSQueue = params->xCoolingQueue;
    TaskId taskId = params->taskId;

    CoolingState_t state = {0};
    state.motorTemp.id = motor_tempId;
    state.inverterTemp.id = inverter_tempId;
    state.tsActive.id = tsActiveId;
    state.motorTemp.timeStatus = STATUS_NOT_READY;
    state.inverterTemp.timeStatus = STATUS_NOT_READY;
    state.tsActive.timeStatus = STATUS_NOT_READY;

    for (;;)
    {
        DataTimeStatus_t motorBefore = state.motorTemp.timeStatus;
        DataTimeStatus_t inverterBefore = state.inverterTemp.timeStatus;
        DataTimeStatus_t tsBefore = state.tsActive.timeStatus;
        bool received = false;

        dataMessage_t msg;
        if (xQueueReceive(*xTCSQueue, &msg, COOLING_TIMEOUT) == pdPASS)
        {
            dataMessage_t *slot = CoolingSlot(&state, msg.id);
            if (slot != NULL)
            {
                msg.timeStatus = STATUS_TIME_OK;
                *slot = msg;
                received = true;
            }
        }

        TickType_t now = xTaskGetTickCount();
        updateDataTimeStatus(&state.motorTemp, 1, TEMP_TIMEOUT_MS, now);
        updateDataTimeStatus(&state.inverterTemp, 1, TEMP_TIMEOUT_MS, now);
        updateDataTimeStatus(&state.tsActive, 1, VEHICLE_STATE_TIMEOUT_MS, now);

        // Nouvelle mesure, ou donnee qui vient d'etre perdue : les ordres sont recalcules
        if (received || (motorBefore != state.motorTemp.timeStatus) ||
            (inverterBefore != state.inverterTemp.timeStatus) || (tsBefore != state.tsActive.timeStatus))
        {
            EvaluateTemperatures(&state);
            PublishOrders(&state);
        }

        wdgFeedNow(taskId);
    }
}
