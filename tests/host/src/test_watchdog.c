// Secteur : watchdog
// Code teste : TaskWDG (src/TaskWDG.c) et l'alimentation du watchdog par TaskCanRx
// Problemes : W1, W3, W4, W5, W6 de AUDIT_VCU.md (W2 est verifie par le test statique)
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
#include "TaskDigitalSignals.h"
#include "TaskCoolingSystem.h"
#include "test_app.h"
#include "vcu_test.h"

#define WDG_PERIOD_MS ((TickType_t)WDG_PERIOD) // periode de TaskWDG (project_config.h)

static unsigned sRefreshCount = 0;
static unsigned sLateRefreshCount = 0; // rafraichissements a partir de sLateFrom
static TickType_t sLateFrom = 0xFFFFFFFFu;
static TickType_t sFeedUntil = 0;

static CanRxParams_t sCanRxParams;
static DigitalSignalsParams_t sDigitalParams;
static CoolingParams_t sCoolingParams;

void test_refreshIWDG(void)
{
    sRefreshCount++;
    if (xTaskGetTickCount() >= sLateFrom)
    {
        sLateRefreshCount++;
    }
}

// Signe de vie d'une tache, donne comme dans les taches du VCU
static void Feed(TaskId taskId, TickType_t now)
{
    wdgFeed(taskId, now);
}

// La tache a-t-elle donne signe de vie depuis since ?
static bool FedSince(TaskId taskId, TickType_t since)
{
    return wdgLastFeed[taskId] >= since;
}

// Appelee a chaque reveil de TaskWDG : TaskCanRx nourrit le watchdog tant que now <= sFeedUntil
static void FeedCanRx(TickType_t now)
{
    if (now <= sFeedUntil)
    {
        Feed(TaskCanRxId, now);
    }
}

static void Setup(void)
{
    memset((void *)wdgLastFeed, 0, sizeof(wdgLastFeed));
    xCanRxQueue = xQueueCreate(10, sizeof(CanFrame_t));
    xTRVPQueue = xQueueCreate(10, sizeof(dataMessage_t));
    xMainQueue = xQueueCreate(10, sizeof(dataMessage_t));
    memset(wdgTimeoutTable, 0, sizeof(WDGTimoutTable));
    wdgRegister(TaskCanRxId, CAN_RX_WDG_TIMEOUT, wdgTimeoutTable); // comme wdgRegisterTasks()

    sCanRxParams = (CanRxParams_t){&xMainQueue, &xTRVPQueue, &xCanRxQueue, TaskCanRxId};
    sRefreshCount = 0;
    sLateRefreshCount = 0;
    sLateFrom = 0xFFFFFFFFu;
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(0);
}

static void TestWatchdog_TableSize(void)
{
    vt_check("W3a", "-", MAX_TASKS >= TASK_COUNT,
             "MAX_TASKS = %d pour %d taches declarees dans TaskId (attendu >= %d)",
             MAX_TASKS, TASK_COUNT, TASK_COUNT);
}

static void TestWatchdog_RefreshWhenHealthy(void)
{
    Setup();
    sFeedUntil = 0xFFFFFFFFu;
    vTestSetDelayCallsBeforeEscape(5, FeedCanRx); // 5 periodes de TaskWDG
    RunTaskOnce(TaskWDG, NULL);
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
    RunTaskOnce(TaskWDG, NULL);
    vTestSetDelayCallsBeforeEscape(0, NULL);

    vt_check("W1b", "-", sLateRefreshCount == 0,
             "TaskCanRx figee depuis 2 s -> IWDG encore rafraichi %u fois apres %lu ms (attendu 0 : la carte doit redemarrer)",
             sLateRefreshCount, (unsigned long)sLateFrom);
}

#define TASK_START_TICK 1000U // heure de lancement des taches ci-dessous : 0 voudrait dire « jamais nourri »

static void TestWatchdog_CanRxWithoutTraffic(void)
{
    Setup();
    vTestSetTickCount(TASK_START_TICK);
    vTestSetBlockingTimeoutsBeforeEscape(10); // jusqu'a 10 attentes avec timeout
    RunTaskOnce(TaskCanRx, &sCanRxParams);
    vTestSetBlockingTimeoutsBeforeEscape(0);

    bool fed = FedSince(TaskCanRxId, TASK_START_TICK);
    vt_check("W4a", "-", fed, "bus CAN silencieux -> TaskCanRx donne signe de vie : %s (attendu oui, sinon declaree morte)",
             fed ? "oui" : "non");
}

// TaskDigitalSignals sans ordre, TaskCoolingSystem sans temperature : elles doivent quand meme
// nourrir le watchdog, sinon elles seraient declarees mortes
static void TestWatchdog_OutputTasksWithoutMessages(void)
{
    Setup();
    xDSQueue = xQueueCreate(10, sizeof(DigitalOrder_t));
    sDigitalParams = (DigitalSignalsParams_t){&xDSQueue, TaskDigitalSignalsId};
    vTestSetTickCount(TASK_START_TICK);
    vTestSetBlockingTimeoutsBeforeEscape(10); // jusqu'a 10 attentes avec timeout
    RunTaskOnce(TaskDigitalSignals, &sDigitalParams);
    vTestSetBlockingTimeoutsBeforeEscape(0);
    bool fed = FedSince(TaskDigitalSignalsId, TASK_START_TICK);
    vt_check("W5a", "-", fed, "aucun ordre de sortie -> TaskDigitalSignals donne signe de vie : %s (attendu oui)",
             fed ? "oui" : "non");

    Setup();
    xDSQueue = xQueueCreate(10, sizeof(DigitalOrder_t));
    xCoolingQueue = xQueueCreate(10, sizeof(dataMessage_t));
    sCoolingParams = (CoolingParams_t){&xCoolingQueue, TaskCoolingId};
    vTestSetTickCount(TASK_START_TICK);
    vTestSetBlockingTimeoutsBeforeEscape(10);
    RunTaskOnce(TaskCoolingSystem, &sCoolingParams);
    vTestSetBlockingTimeoutsBeforeEscape(0);
    fed = FedSince(TaskCoolingId, TASK_START_TICK);
    vt_check("W5b", "-", fed, "aucune temperature -> TaskCoolingSystem donne signe de vie : %s (attendu oui)",
             fed ? "oui" : "non");
}

// Avec du trafic CAN, les taches rapides donnent signe de vie a chaque trame : 3 fois chacune par
// periode de TaskWDG, puis chaque tache lente une fois
static void FeedWithTraffic(TickType_t now)
{
    static const TaskId fastTasks[] = {TaskCanRxId, TaskTRVPId, TaskMainId, TaskCanTxId};
    static const TaskId slowTasks[] = {TaskIHMId, TaskDigitalSignalsId, TaskCoolingId};
    for (unsigned round = 0; round < 3U; round++)
    {
        for (unsigned i = 0; i < sizeof(fastTasks) / sizeof(fastTasks[0]); i++)
        {
            Feed(fastTasks[i], now);
        }
    }
    for (unsigned i = 0; i < sizeof(slowTasks) / sizeof(slowTasks[0]); i++)
    {
        Feed(slowTasks[i], now);
    }
}

// W6 : les signes de vie des taches lentes ne doivent pas se perdre derriere ceux des taches rapides
static void TestWatchdog_TrafficKeepsSlowTasks(void)
{
    Setup();
    wdgRegisterTasks();     // les 7 taches, avec leurs delais, comme MainApp()
    vTestSetTickCount(2000U); // une tache qui n'a jamais donne signe de vie est deja en retard
    vTestSetDelayCallsBeforeEscape(3, FeedWithTraffic); // 3 periodes de TaskWDG
    RunTaskOnce(TaskWDG, NULL);
    vTestSetDelayCallsBeforeEscape(0, NULL);

    vt_check("W6a", "-", sRefreshCount == 3,
             "3 periodes avec trafic (12 signes de vie des taches rapides, puis 1 par tache lente) -> IWDG rafraichi "
             "%u fois (attendu 3 : aucune tache lente ne doit paraitre en retard)",
             sRefreshCount);
}

int main(void)
{
    vt_begin("Watchdog");

    TestWatchdog_TableSize();
    TestWatchdog_RefreshWhenHealthy();
    TestWatchdog_NoRefreshWhenStalled();
    TestWatchdog_CanRxWithoutTraffic();
    TestWatchdog_OutputTasksWithoutMessages();
    TestWatchdog_TrafficKeepsSlowTasks();

    return vt_end();
}
