
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"

#include "console.h"

#include "TaskCanRX.h"
#include "TaskMain.h"
#include "VCU.h"
#include "throttle.h"
#include "utils.h"
#include "TaskCanTx.h"
#include "TaskRawValuesProcessing.h"

#ifdef LINUX
#include "CanISREmulator.h"
#endif

void MainApp(void)
{

    QueueHandle_t xMainQueue = xQueueCreate(10, sizeof(dataMessage_t));
    QueueHandle_t xTRVPQueue = xQueueCreate(10, sizeof(dataMessage_t));
    QueueHandle_t xIHMQueue = xQueueCreate(10, sizeof(IHMOrder_t));
    QueueHandle_t xCanTxQueue = xQueueCreate(10, sizeof(dataMessage_t));

    initThrottleValues();
    // GetUserThrottleCommand();

    MainParams_t mainTaskParam = {&xMainQueue, &xTRVPQueue, &xIHMQueue, &xCanTxQueue};
    CanRxParams_t canRxParams = {&xMainQueue, &xTRVPQueue};
    CanTxParam_t canTxParams = {&xCanTxQueue};
    TRVPParams_t TRVPParams = {&xTRVPQueue, &xMainQueue};
    xTaskCreate(
        TaskCanRx,
        "CAN_RX",
        configMINIMAL_STACK_SIZE,
        &canRxParams,
        2U,
        NULL);

    /*xTaskCreate(
        TaskMain,
        "MAIN",
        configMINIMAL_STACK_SIZE,
        &mainTaskParam,
        2U,
        NULL);

    xTaskCreate(
        TaskCanTx,
        "CAN_TX",
        configMINIMAL_STACK_SIZE,
        &canTxParams,
        tskIDLE_PRIORITY,
        NULL);

    xTaskCreate(
        TaskRawValuesProcessing,
        "TRVP",
        configMINIMAL_STACK_SIZE,
        &TRVPParams,
        2U,
        NULL);*/

#ifdef LINUX
    xTaskCreate(
        CanISREmulator,
        "CanRxIsr",
        configMINIMAL_STACK_SIZE,
        NULL,
        tskIDLE_PRIORITY,
        NULL);

#endif

    vTaskStartScheduler();

    for (;;)
    {
    }
}

void initThrottleValues()
{
    potmin[0] = 100;      // ADC raw min for pedal sensor 1
    potmin[1] = 100;      // ADC raw min for pedal sensor 2
    potmax[0] = 3900;     // ADC raw max for pedal sensor 1
    potmax[1] = 3900;     // ADC raw max for pedal
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
