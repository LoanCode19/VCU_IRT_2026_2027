#include <stdio.h>
#include <setjmp.h>
#include <stdbool.h>

#include "queue.h"
#include "task.h"

#include "structs.h"
#include "queueHandles.h"
#include "TaskCoolingSystem.h"
#include "TaskDigitalSignals.h"

static int sFailures = 0;

#define TEST_ASSERT(cond, message)                 \
    do                                              \
    {                                               \
        if (cond)                                   \
        {                                           \
            printf("PASS: %s\n", message);        \
        }                                           \
        else                                        \
        {                                           \
            printf("FAIL: %s\n", message);        \
            sFailures++;                            \
        }                                           \
    } while (0)

static void RunCoolingPublishesToDigitalQueueTest(void)
{
    jmp_buf env;

    vTestSetTickCount(0U);

    QueueHandle_t xTCSQueue = xQueueCreate(8U, sizeof(dataMessage_t));
    xDSQueue = xQueueCreate(8U, sizeof(DigitalOrder_t));

    TEST_ASSERT(xTCSQueue != NULL, "xTCSQueue created");
    TEST_ASSERT(xDSQueue != NULL, "xDSQueue created");

    dataMessage_t motorTempMsg = {
        .id = motor_tempId,
        .data = 56,
        .timestamp = 0,
        .timeStatus = STATUS_TIME_OK};

    TEST_ASSERT(
        xQueueSend(xTCSQueue, &motorTempMsg, 0) == pdPASS,
        "motor temperature message queued for cooling task");

    if (setjmp(env) == 0)
    {
        vTestEnableEscapeWhenQueueEmpty(&env);
        TaskCoolingSystem(&(CoolingParams_t){&xTCSQueue, NULL, TaskCoolingId});
    }
    vTestDisableEscapeWhenQueueEmpty();

    TEST_ASSERT(
        uxTestQueueCount(xDSQueue) == 2U,
        "cooling task published 2 digital orders (pump + fans)");

    DigitalOrder_t firstOrder = {0};
    DigitalOrder_t secondOrder = {0};

    TEST_ASSERT(xQueueReceive(xDSQueue, &firstOrder, 0) == pdPASS, "first digital order readable");
    TEST_ASSERT(xQueueReceive(xDSQueue, &secondOrder, 0) == pdPASS, "second digital order readable");

    TEST_ASSERT(firstOrder.order == CoolingPump, "first order is CoolingPump");
    TEST_ASSERT(firstOrder.state == true, "CoolingPump state is ON at 56C");

    TEST_ASSERT(secondOrder.order == CoolingFans, "second order is CoolingFans");
    TEST_ASSERT(secondOrder.state == true, "CoolingFans state is ON at 56C");
}

static void RunCoolingStaysOffBelowThresholdTest(void)
{
    jmp_buf env;

    vTestSetTickCount(0U);

    QueueHandle_t xTCSQueue = xQueueCreate(8U, sizeof(dataMessage_t));
    xDSQueue = xQueueCreate(8U, sizeof(DigitalOrder_t));

    TEST_ASSERT(xTCSQueue != NULL, "xTCSQueue created for low temp test");
    TEST_ASSERT(xDSQueue != NULL, "xDSQueue created for low temp test");

    dataMessage_t motorTempMsg = {
        .id = motor_tempId,
        .data = 40,
        .timestamp = 0,
        .timeStatus = STATUS_TIME_OK};

    TEST_ASSERT(
        xQueueSend(xTCSQueue, &motorTempMsg, 0) == pdPASS,
        "low motor temperature message queued for cooling task");

    if (setjmp(env) == 0)
    {
        vTestEnableEscapeWhenQueueEmpty(&env);
        TaskCoolingSystem(&(CoolingParams_t){&xTCSQueue, NULL, TaskCoolingId});
    }
    vTestDisableEscapeWhenQueueEmpty();

    TEST_ASSERT(
        uxTestQueueCount(xDSQueue) == 2U,
        "cooling task published 2 digital orders for low temp");

    DigitalOrder_t firstOrder = {0};
    DigitalOrder_t secondOrder = {0};

    TEST_ASSERT(xQueueReceive(xDSQueue, &firstOrder, 0) == pdPASS, "first low-temp digital order readable");
    TEST_ASSERT(xQueueReceive(xDSQueue, &secondOrder, 0) == pdPASS, "second low-temp digital order readable");

    TEST_ASSERT(firstOrder.order == CoolingPump, "low-temp first order is CoolingPump");
    TEST_ASSERT(firstOrder.state == false, "CoolingPump state is OFF at 40C");

    TEST_ASSERT(secondOrder.order == CoolingFans, "low-temp second order is CoolingFans");
    TEST_ASSERT(secondOrder.state == false, "CoolingFans state is OFF at 40C");
}

static void RunCoolingDoesNotRepublishUnchangedStateTest(void)
{
    jmp_buf env;

    vTestSetTickCount(0U);

    QueueHandle_t xTCSQueue = xQueueCreate(8U, sizeof(dataMessage_t));
    xDSQueue = xQueueCreate(8U, sizeof(DigitalOrder_t));

    TEST_ASSERT(xTCSQueue != NULL, "xTCSQueue created for unchanged-state test");
    TEST_ASSERT(xDSQueue != NULL, "xDSQueue created for unchanged-state test");

    dataMessage_t firstTempMsg = {
        .id = motor_tempId,
        .data = 56,
        .timestamp = 1,
        .timeStatus = STATUS_TIME_OK};

    dataMessage_t secondTempMsg = {
        .id = motor_tempId,
        .data = 57,
        .timestamp = 2,
        .timeStatus = STATUS_TIME_OK};

    TEST_ASSERT(xQueueSend(xTCSQueue, &firstTempMsg, 0) == pdPASS, "first unchanged-state message queued");
    TEST_ASSERT(xQueueSend(xTCSQueue, &secondTempMsg, 0) == pdPASS, "second unchanged-state message queued");

    if (setjmp(env) == 0)
    {
        vTestEnableEscapeWhenQueueEmpty(&env);
        TaskCoolingSystem(&(CoolingParams_t){&xTCSQueue, NULL, TaskCoolingId});
    }
    vTestDisableEscapeWhenQueueEmpty();

    TEST_ASSERT(
        uxTestQueueCount(xDSQueue) == 2U,
        "cooling task does not republish when ON/OFF state is unchanged");
}

static void RunCoolingTimeoutForcesFailsafeTest(void)
{
    jmp_buf env;

    vTestSetTickCount(1000U);

    QueueHandle_t xTCSQueue = xQueueCreate(8U, sizeof(dataMessage_t));
    xDSQueue = xQueueCreate(8U, sizeof(DigitalOrder_t));

    TEST_ASSERT(xTCSQueue != NULL, "xTCSQueue created for timeout test");
    TEST_ASSERT(xDSQueue != NULL, "xDSQueue created for timeout test");

    dataMessage_t staleTempMsg = {
        .id = motor_tempId,
        .data = 30,
        .timestamp = 0,
        .timeStatus = STATUS_TIME_OK};

    TEST_ASSERT(
        xQueueSend(xTCSQueue, &staleTempMsg, 0) == pdPASS,
        "stale motor temperature message queued for timeout test");

    if (setjmp(env) == 0)
    {
        vTestEnableEscapeWhenQueueEmpty(&env);
        TaskCoolingSystem(&(CoolingParams_t){&xTCSQueue, NULL, TaskCoolingId});
    }
    vTestDisableEscapeWhenQueueEmpty();

    TEST_ASSERT(
        uxTestQueueCount(xDSQueue) == 2U,
        "cooling task publishes failsafe orders when message is timed out");

    DigitalOrder_t firstOrder = {0};
    DigitalOrder_t secondOrder = {0};

    TEST_ASSERT(xQueueReceive(xDSQueue, &firstOrder, 0) == pdPASS, "first timeout order readable");
    TEST_ASSERT(xQueueReceive(xDSQueue, &secondOrder, 0) == pdPASS, "second timeout order readable");

    TEST_ASSERT(firstOrder.order == CoolingPump, "timeout first order is CoolingPump");
    TEST_ASSERT(firstOrder.state == true, "CoolingPump forced ON when data timed out");

    TEST_ASSERT(secondOrder.order == CoolingFans, "timeout second order is CoolingFans");
    TEST_ASSERT(secondOrder.state == true, "CoolingFans forced ON when data timed out");
}

static void RunDigitalTaskConsumesQueueTest(void)
{
    jmp_buf env;

    vTestSetTickCount(0U);

    if (xDSQueue == NULL)
    {
        xDSQueue = xQueueCreate(8U, sizeof(DigitalOrder_t));
    }

    DigitalOrder_t pumpOrder = {.order = CoolingPump, .state = true};
    DigitalOrder_t fansOrder = {.order = CoolingFans, .state = false};

    TEST_ASSERT(xQueueSend(xDSQueue, &pumpOrder, 0) == pdPASS, "pump order queued for digital task");
    TEST_ASSERT(xQueueSend(xDSQueue, &fansOrder, 0) == pdPASS, "fans order queued for digital task");
    TEST_ASSERT(uxTestQueueCount(xDSQueue) == 2U, "digital queue has 2 messages before task runs");

    if (setjmp(env) == 0)
    {
        vTestEnableEscapeWhenQueueEmpty(&env);
        TaskDigitalSignals(&(DigitalSignalsParams_t){&xDSQueue, NULL, TaskDigitalSignalsId});
    }
    vTestDisableEscapeWhenQueueEmpty();

    TEST_ASSERT(
        uxTestQueueCount(xDSQueue) == 0U,
        "digital task consumed queue messages without output action");
}

int main(void)
{
    int exitCode = 0;

    printf("Running host tests (no STM32 hardware)...\n\n");

    RunCoolingPublishesToDigitalQueueTest();
    RunCoolingStaysOffBelowThresholdTest();
    RunCoolingDoesNotRepublishUnchangedStateTest();
    RunCoolingTimeoutForcesFailsafeTest();
    RunDigitalTaskConsumesQueueTest();

    printf("\n");
    if (sFailures == 0)
    {
        printf("ALL HOST TESTS PASSED\n");
        exitCode = 0;
    }
    else
    {
        printf("HOST TESTS FAILED: %d failure(s)\n", sFailures);
        exitCode = 1;
    }

    printf("Press Enter to exit...\n");
    (void)getchar();
    return exitCode;
}
