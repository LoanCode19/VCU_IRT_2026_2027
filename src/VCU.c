
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

#ifdef TEST_ISR
#include "CanISREmulator.h"
#endif

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

    xMainQueue = xQueueCreate(10, sizeof(dataMessage_t));
    configASSERT(xMainQueue != NULL);
    xTRVPQueue = xQueueCreate(10, sizeof(dataMessage_t));
    configASSERT(xTRVPQueue != NULL);
    xIHMQueue = xQueueCreate(10, sizeof(IHMOrder_t));
    configASSERT(xIHMQueue != NULL);
    xCanTxQueue = xQueueCreate(10, sizeof(dataMessage_t));
    configASSERT(xCanTxQueue != NULL);
    xCanRxQueue = xQueueCreate(10, sizeof(dataCan));
    configASSERT(xCanRxQueue != NULL);
    xWDGQueue = xQueueCreate(10, sizeof(WdgMessage_t));
    configASSERT(xWDGQueue != NULL);
    xDSQueue = xQueueCreate(10, sizeof(DigitalOrder_t));
    configASSERT(xDSQueue != NULL);
    xCoolingQueue = xQueueCreate(10, sizeof(dataMessage_t));
    configASSERT(xCoolingQueue != NULL);

    initThrottleValues();
    // GetUserThrottleCommand();

    // static : les taches utilisent ces parametres bien apres la fin de MainApp()
    static MainParams_t mainTaskParam = {&xMainQueue, &xTRVPQueue, &xIHMQueue, &xCanTxQueue, &xWDGQueue, TaskMainId};
    static CanRxParams_t canRxParams = {&xMainQueue, &xTRVPQueue, &xCanRxQueue, &xWDGQueue, TaskCanRxId, &xCoolingQueue};
    static CanTxParam_t canTxParams = {&xCanTxQueue, &xWDGQueue, TaskCanTxId};
    static TRVPParams_t TRVPParams = {&xTRVPQueue, &xMainQueue, &xWDGQueue, TaskTRVPId};
    static TaskIHMParam_t ihmParams = {&xIHMQueue, &xDSQueue, &xWDGQueue, TaskIHMId};
#ifdef USE_WDG
    static WdgParams_t wdgParams = {&xWDGQueue};
#endif

    // Priorite : plus le nombre est grand, plus la tache est prioritaire. Pile en mots de 4 octets.
    CreateTask(TaskCanRx, "CAN_RX", 256, &canRxParams, 6);
    CreateTask(TaskMain, "MAIN", 512, &mainTaskParam, 5);
    CreateTask(TaskCanTx, "CAN_TX", 512, &canTxParams, 5);
    CreateTask(TaskRawValuesProcessing, "TRVP", 512, &TRVPParams, 4);
    CreateTask(TaskDigitalSignals, "DIGITAL", 256, &xDSQueue, 3);
    CreateTask(TaskCoolingSystem, "COOLING", 256, &xCoolingQueue, 2);
    CreateTask(TaskIHM, "IHM", 256, &ihmParams, 2);

#ifdef TEST_ISR
    CreateTask(CanISREmulator, "CanRxIsr", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY);
#endif
#ifdef USE_WDG
    CreateTask(TaskWDG, "WDG", 256, &wdgParams, 1);

    wdgRegisterTasks();

#endif
}

#ifdef USE_WDG
void wdgRegisterTasks()
{
    /*wdgRegister(TaskMainId, MAIN_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskTRVPId, TRVP_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskIHMId, IHM_WDG_TIMEOUT, wdgTimeoutTable);
    wdgRegister(TaskCanTxId, CAN_TX_WDG_TIMEOUT, wdgTimeoutTable);*/
    wdgRegister(TaskCanRxId, CAN_RX_WDG_TIMEOUT, wdgTimeoutTable);
}
#endif

void initThrottleValues()
{
    potmin[0] = 100;      // ADC raw min for pedal sensor 1
    potmin[1] = 100;      // ADC raw min for pedal sensor 2
    potmax[0] = 3900;     // ADC raw max for pedal sensor 1
    potmax[1] = 3900;     // ADC raw max for pedal sensor 2
    regenRpm = 1000.0f;   // Start applying regen above this RPM
    regenendRpm = 200.0f; // Regen fades out below this RPM
    brknompedal = 0.0f;   // Neutral brake pedal level
    regenmax = 30.0f;     // Max regen % (negative torque)
    regenBrake = 50.0f;   // Regen when brake pedal pressed (%)
    brkcruise = 0.0f;     // Brake threshold to

    idleSpeed = 800; // Target idle speed in rpm
    cruiseSpeed = 0; // Default cruise setpoint (none)
    speedkp = 0.2f;  // PID proportional gain for cruise
    speedflt = 50;   // Filter time constant

    idleThrotLim = 5.0f; // Throttle limit during idle hold (%)
    throtmax = 100.0f;   // Max throttle %
    throtmaxRev = 40.0f; // Max throttle in reverse %
    throtmin = 0.0f;     // Min throttle limit (%)
    throtdead = 2.0f;    // Deadband around 0% p

    regenRamp = 2.0f;    // %/10ms rate of change for regen
    throtRamp = 3.0f;    // %/10ms rate of chang
    throtRampMax = 5.0f; // %/10ms rate of chang
    throtRampRpm = 2000; // RPM at which to switch from throtRamp to throtRampMax

    udcmin = 250.0f;  // Min DC bus voltage (V)
    udcmax = 410.0f;  // Max DC bus voltage (V)
    idcmin = -400.0f; // Min DC current (regen limit, A)
    idcmax = 400.0f;  // Max DC current (drive limit, A)

    speedLimit = 160;     // km/h speed limiter
    ThrotRpmFilt = 50.0f; // Filtered RPM used for throttle logic

    motorTempMax = 100.0f;
    inverterTempMax = 90.0f;
}
