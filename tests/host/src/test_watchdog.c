// Secteur : watchdog
// Code teste : TaskWDG (src/TaskWDG.c) et l'alimentation du watchdog par TaskCanRx
// Problemes : W1, W3, W4 de AUDIT_VCU.md (W2 est verifie par le test statique)
//
// Compile avec -DUSE_WDG et -DrefreshIWDG=test_refreshIWDG : ce renommage remplace le
// rafraichissement de l'IWDG de la carte par une fonction de test qui compte les appels.
// Sans LINUX, c'est bien la branche de la carte qui est testee.

#include <stdbool.h>
#include <string.h>

#include "structs.h"
#include "project_config.h"
#include "queueHandles.h"
#include "utils.h"
#include "TaskCanRX.h"
#include "TaskWDG.h"
#include "test_app.h"
#include "vcu_test.h"

#define WDG_PERIOD_MS ((TickType_t)WDG_PERIOD) // periode de TaskWDG (project_config.h)

static unsigned sRefreshCount = 0;
static unsigned sLateRefreshCount = 0; // rafraichissements a partir de sLateFrom
static TickType_t sLateFrom = 0xFFFFFFFFu;
static TickType_t sFeedUntil = 0;

static WdgParams_t sWdgParams;
static CanRxParams_t sCanRxParams;

void test_refreshIWDG(void)
{
    sRefreshCount++;
    if (xTaskGetTickCount() >= sLateFrom)
    {
        sLateRefreshCount++;
    }
}

// Appelee a chaque reveil de TaskWDG : TaskCanRx nourrit le watchdog tant que now <= sFeedUntil
static void FeedCanRx(TickType_t now)
{
    if (now <= sFeedUntil)
    {
        WdgMessage_t msg = {.taskId = TaskCanRxId, .timestamp = now};
        (void)xQueueSend(xWDGQueue, &msg, 0);
    }
}

static void Setup(void)
{
    xWDGQueue = xQueueCreate(10, sizeof(WdgMessage_t));
    xCanRxQueue = xQueueCreate(10, sizeof(dataCan));
    xTRVPQueue = xQueueCreate(10, sizeof(dataMessage_t));
    xMainQueue = xQueueCreate(10, sizeof(dataMessage_t));
    memset(wdgTimeoutTable, 0, sizeof(WDGTimoutTable));
    wdgRegister(TaskCanRxId, CAN_RX_WDG_TIMEOUT, wdgTimeoutTable); // comme wdgRegisterTasks()

    sWdgParams = (WdgParams_t){&xWDGQueue, 0};
    sCanRxParams = (CanRxParams_t){&xMainQueue, &xTRVPQueue, &xCanRxQueue, &xWDGQueue, TaskCanRxId};
    sRefreshCount = 0;
    sLateRefreshCount = 0;
    sLateFrom = 0xFFFFFFFFu;
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(0);
}

static void TestWatchdog_TableSize(void)
{
    vt_check("W3a", "-", MAX_TASKS >= (TaskMainId + 1),
             "MAX_TASKS = %d pour %d taches declarees dans TaskId (attendu >= %d)",
             MAX_TASKS, TaskMainId + 1, TaskMainId + 1);
}

static void TestWatchdog_RefreshWhenHealthy(void)
{
    Setup();
    sFeedUntil = 0xFFFFFFFFu;
    vTestSetDelayCallsBeforeEscape(5, FeedCanRx); // 5 periodes de TaskWDG
    RunTaskOnce(TaskWDG, &sWdgParams);
    vTestSetDelayCallsBeforeEscape(0, NULL);

    vt_check("W1a", "-", sRefreshCount == 5,
             "5 periodes de %lu ms, toutes les taches a l'heure -> IWDG rafraichi %u fois (attendu 5)",
             (unsigned long)WDG_PERIOD_MS, sRefreshCount);
}

static void TestWatchdog_NoRefreshWhenStalled(void)
{
    Setup();
    sFeedUntil = 2000U; // TaskCanRx se fige apres t = 2 s
    // Dernier signe de vie a 2000 ms : en retard apres son timeout, constate a la periode suivante
    sLateFrom = sFeedUntil + CAN_RX_WDG_TIMEOUT + WDG_PERIOD_MS;
    vTestSetDelayCallsBeforeEscape(6000U / WDG_PERIOD_MS, FeedCanRx); // jusqu'a t = 6 s
    RunTaskOnce(TaskWDG, &sWdgParams);
    vTestSetDelayCallsBeforeEscape(0, NULL);

    vt_check("W1b", "-", sLateRefreshCount == 0,
             "TaskCanRx figee depuis 2 s -> IWDG encore rafraichi %u fois apres %lu ms (attendu 0 : la carte doit redemarrer)",
             sLateRefreshCount, (unsigned long)sLateFrom);
}

static void TestWatchdog_CanRxWithoutTraffic(void)
{
    Setup();
    vTestSetBlockingTimeoutsBeforeEscape(10); // jusqu'a 10 attentes avec timeout
    RunTaskOnce(TaskCanRx, &sCanRxParams);
    vTestSetBlockingTimeoutsBeforeEscape(0);

    unsigned feeds = (unsigned)uxTestQueueCount(xWDGQueue);
    vt_check("W4a", "-", feeds >= 1U,
             "bus CAN silencieux -> TaskCanRx nourrit le watchdog %u fois (attendu >= 1, sinon declaree morte)",
             feeds);
}

int main(void)
{
    vt_begin("Watchdog");

    TestWatchdog_TableSize();
    TestWatchdog_RefreshWhenHealthy();
    TestWatchdog_NoRefreshWhenStalled();
    TestWatchdog_CanRxWithoutTraffic();

    return vt_end();
}
