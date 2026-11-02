
#include <stdio.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "queueHandles.h"

#include "structs.h"

#include "console.h"

#include "TaskCanRX.h"
#include "TaskMain.h"
#include "TaskIHM.h"
#include "VCU.h"
#include "throttle.h"
#include "utils.h"
#include "TaskCanTx.h"
#include "TaskRawValuesProcessing.h"
#include "TaskWDG.h"
#include "TaskDigitalSignals.h"
#include "TaskCoolingSystem.h"

// Cree une tache, et bloque si la creation echoue (tas FreeRTOS trop petit)
static void CreateTask(TaskFunction_t task, const char *name, uint16_t stackWords,
                       void *params, UBaseType_t priority)
{
    BaseType_t created = xTaskCreate(task, name, stackWords, params, priority, NULL);
    configASSERT(created == pdPASS);
}

// Cree les queues et les taches du VCU. Le scheduler est demarre ensuite par l'appelant
// (osKernelStart() dans main.c sur la carte).
void MainApp(void)
{
    console_init(); // R7 : mutex des traces, avant que les taches ecrivent

    xMainQueue = xQueueCreate(MAIN_QUEUE_LENGTH, sizeof(dataMessage_t));
    configASSERT(xMainQueue != NULL);
    xTRVPQueue = xQueueCreate(TRVP_QUEUE_LENGTH, sizeof(dataMessage_t));
    configASSERT(xTRVPQueue != NULL);
    xIHMQueue = xQueueCreate(IHM_QUEUE_LENGTH, sizeof(IHMOrder_t));
    configASSERT(xIHMQueue != NULL);
    xCanTxQueue = xQueueCreate(CAN_TX_QUEUE_LENGTH, sizeof(dataMessage_t));
    configASSERT(xCanTxQueue != NULL);
    xCanRxQueue = xQueueCreate(CAN_RX_QUEUE_LENGTH, sizeof(CanFrame_t));
    configASSERT(xCanRxQueue != NULL);
    xDSQueue = xQueueCreate(DS_QUEUE_LENGTH, sizeof(DigitalOrder_t));
    configASSERT(xDSQueue != NULL);
    xCoolingQueue = xQueueCreate(COOLING_QUEUE_LENGTH, sizeof(dataMessage_t));
    configASSERT(xCoolingQueue != NULL);

    initThrottleValues();

    // static : les taches utilisent ces parametres bien apres la fin de MainApp()
    static MainParams_t mainTaskParam = {&xMainQueue, &xTRVPQueue, &xIHMQueue, &xCanTxQueue, TaskMainId};
    static CanRxParams_t canRxParams = {&xMainQueue, &xTRVPQueue, &xCanRxQueue, TaskCanRxId, &xCoolingQueue};
    static CanTxParam_t canTxParams = {&xCanTxQueue, TaskCanTxId};
    static TRVPParams_t TRVPParams = {&xTRVPQueue, &xMainQueue, TaskTRVPId};
    static TaskIHMParam_t ihmParams = {&xIHMQueue, &xDSQueue, TaskIHMId};
    static DigitalSignalsParams_t digitalSignalsParams = {&xDSQueue, TaskDigitalSignalsId};
    static CoolingParams_t coolingParams = {&xCoolingQueue, TaskCoolingId, &xDSQueue};

    // Priorite : plus le nombre est grand, plus la tache est prioritaire. Pile en mots de 4 octets.
    // TRVP au-dessus de Main (R9, T28, decide le 01/11/2026, revisable) : reveillees au meme tick, TRVP passe
    // d'abord et Main lit la consigne de la meme periode.
    CreateTask(TaskCanRx, "CAN_RX", 256, &canRxParams, 6);
    CreateTask(TaskMain, "MAIN", 512, &mainTaskParam, 4);
    CreateTask(TaskCanTx, "CAN_TX", 512, &canTxParams, 5);
    CreateTask(TaskRawValuesProcessing, "TRVP", 512, &TRVPParams, 5);
    CreateTask(TaskDigitalSignals, "DIGITAL", 256, &digitalSignalsParams, 3);
    CreateTask(TaskCoolingSystem, "COOLING", 256, &coolingParams, 2);
    CreateTask(TaskIHM, "IHM", 256, &ihmParams, 2);

#ifdef USE_WDG
    static TaskWDGParams_t wdgParams = {&xIHMQueue}; // LED de vie, par TaskIHM (T24)
    CreateTask(TaskWDG, "WDG", 256, &wdgParams, 1);

    wdgRegisterTasks();

#endif
}

#ifdef USE_WDG
void wdgRegisterTasks()
{
    wdgRegister(TaskMainId, MAIN_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskTRVPId, TRVP_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskIHMId, IHM_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskCanTxId, CAN_TX_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskCanRxId, CAN_RX_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskDigitalSignalsId, DS_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskCoolingId, COOLING_WDG_TIMEOUT, wdgTimeoutTable);
}
#endif

void initThrottleValues()
{
    potmin[0] = 100;      // ADC raw min for pedal sensor 1
    potmin[1] = 100;      // ADC raw min for pedal sensor 2
    potmax[0] = 3900;     // ADC raw max for pedal sensor 1
    potmax[1] = 3900;     // ADC raw max for pedal sensor 2
    // Pas de recuperation (D3) : ni couple negatif, ni reglage de recuperation
    throtmax = 100.0f; // consigne maximale, en %
    throtmin = 0.0f;   // consigne minimale, en %
    throtdead = 2.0f;  // zone morte en bas de course de la pedale, en %

    // Rampes de montee en %/s (R1) : les 3 et 5 %/10 ms d'avant. Pas de rampe de recuperation (D3).
    throtRampPerS = 300.0f;    // %/s sous throtRampSpeedMs
    throtRampMaxPerS = 500.0f; // %/s au-dessus
    throtRampSpeedMs = 41.9f;  // m/s (T18, decide le 27/10/2026, revisable : les 2000 tr/min d'avant avec D7)

    udcmin = 250.0f; // Min DC bus voltage (V) : provisoire, batterie inconnue (T11)
    // Tension basse (S8, T11, decide le 27/10/2026, revisable) : 100 % de couple a udcmin + 20 V, 0 a udcmin
    udcDerateMarginV = 20.0f;
    // Courant et puissance (S8) : lot 6c, apres D4

    // Limiteur de vitesse (B3, T12, revisable) : 160 km/h, reduction lineaire sur les 3 derniers m/s
    speedLimitMs = 160.0f / 3.6f;
    speedLimitMarginMs = 3.0f;
    ThrotRpmFilt = 50.0f; // Filtered RPM used for throttle logic

    motorTempMax = 100.0f;
    inverterTempMax = 90.0f;

    ResetThrottleState(); // S9 (T10) : pas de verrou frein + accelerateur au demarrage
}
