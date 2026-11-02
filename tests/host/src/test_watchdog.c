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
#include "TaskCanTx.h"
#include "TaskIHM.h"
#include "TaskMain.h"
#include "TaskRawValuesProcessing.h"
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
static CanTxParam_t sCanTxParams;
static TaskIHMParam_t sIhmParams;
static TRVPParams_t sTrvpParams;
static MainParams_t sMainParams;
static TaskWDGParams_t sWdgParams; // queue de TaskIHM, pour la LED de vie (T24)

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
    xIHMQueue = xQueueCreate(IHM_QUEUE_LENGTH, sizeof(IHMOrder_t));
    sWdgParams = (TaskWDGParams_t){&xIHMQueue};
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

// Toutes les taches de TaskId sont surveillees au demarrage (wdgRegisterTasks, VCU.c), et un numero de tache hors du
// tableau n'ecrit pas a cote
static void TestWatchdog_Registration(void)
{
    memset(wdgTimeoutTable, 0, sizeof(WDGTimoutTable));
    wdgRegisterTasks();
    unsigned missing = 0U;
    for (int i = 0; i < TASK_COUNT; i++)
    {
        if (wdgTimeoutTable[i] == 0U)
        {
            missing++;
            vt_note("tache %d non enregistree", i);
        }
    }
    vt_check("W3c", "-", missing == 0U,
             "wdgRegisterTasks() -> %u tache(s) de TaskId sans delai de watchdog (attendu 0 : toutes surveillees)",
             missing);

    // W7 (T28, decide le 01/11/2026, revisable) : TaskCanRx n'a pas un delai plus long que les autres taches. Avec
    // 1 000 ms, une reception figee mettait jusqu'a 2 s a redemarrer la carte.
    TickType_t longestOther = 0U;
    for (int i = 0; i < TASK_COUNT; i++)
    {
        if ((i != TaskCanRxId) && (wdgTimeoutTable[i] > longestOther))
        {
            longestOther = wdgTimeoutTable[i];
        }
    }
    vt_check("W7a", "-", wdgTimeoutTable[TaskCanRxId] <= longestOther,
             "delai de watchdog de TaskCanRx %lu ms, le plus long des autres taches %lu ms (attendu pas plus long)",
             (unsigned long)wdgTimeoutTable[TaskCanRxId], (unsigned long)longestOther);

    struct
    {
        WDGTimoutTable table;
        TickType_t after; // juste apres le tableau
    } guarded = {{0}, 0xA5A5A5A5u};
    wdgRegister((TaskId)MAX_TASKS, 123U, guarded.table);
    vt_check("W3b", "-", guarded.after == 0xA5A5A5A5u,
             "wdgRegister() avec le numero de tache %d (hors du tableau) -> case suivante %s (attendu intacte)",
             MAX_TASKS, (guarded.after == 0xA5A5A5A5u) ? "intacte" : "ecrasee");
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
    sCoolingParams = (CoolingParams_t){&xCoolingQueue, TaskCoolingId, &xDSQueue};
    vTestSetTickCount(TASK_START_TICK);
    vTestSetBlockingTimeoutsBeforeEscape(10);
    RunTaskOnce(TaskCoolingSystem, &sCoolingParams);
    vTestSetBlockingTimeoutsBeforeEscape(0);
    fed = FedSince(TaskCoolingId, TASK_START_TICK);
    vt_check("W5b", "-", fed, "aucune temperature -> TaskCoolingSystem donne signe de vie : %s (attendu oui)",
             fed ? "oui" : "non");

    Setup();
    xCanTxQueue = xQueueCreate(10, sizeof(dataMessage_t));
    sCanTxParams = (CanTxParam_t){&xCanTxQueue, TaskCanTxId};
    vTestSetTickCount(TASK_START_TICK);
    vTestSetBlockingTimeoutsBeforeEscape(10);
    RunTaskOnce(TaskCanTx, &sCanTxParams);
    vTestSetBlockingTimeoutsBeforeEscape(0);
    fed = FedSince(TaskCanTxId, TASK_START_TICK);
    vt_check("W5c", "-", fed, "aucune consigne -> TaskCanTx donne signe de vie : %s (attendu oui)", fed ? "oui" : "non");

    Setup();
    xIHMQueue = xQueueCreate(10, sizeof(IHMOrder_t));
    xDSQueue = xQueueCreate(10, sizeof(DigitalOrder_t));
    sIhmParams = (TaskIHMParam_t){&xIHMQueue, &xDSQueue, TaskIHMId};
    vTestSetTickCount(TASK_START_TICK);
    vTestSetBlockingTimeoutsBeforeEscape(10);
    RunTaskOnce(TaskIHM, &sIhmParams);
    vTestSetBlockingTimeoutsBeforeEscape(0);
    fed = FedSince(TaskIHMId, TASK_START_TICK);
    vt_check("W5d", "-", fed, "aucun ordre -> TaskIHM donne signe de vie : %s (attendu oui)", fed ? "oui" : "non");

    // Les deux taches periodiques donnent signe de vie a chaque periode, meme sans aucun message
    Setup();
    xIHMQueue = xQueueCreate(10, sizeof(IHMOrder_t));
    xCanTxQueue = xQueueCreate(10, sizeof(dataMessage_t));
    sTrvpParams = (TRVPParams_t){&xTRVPQueue, &xMainQueue, TaskTRVPId};
    sMainParams = (MainParams_t){&xMainQueue, &xTRVPQueue, &xIHMQueue, &xCanTxQueue, TaskMainId};
    vTestSetTickCount(TASK_START_TICK);
    RunTaskOnce(TaskRawValuesProcessing, &sTrvpParams);
    RunTaskOnce(TaskMain, &sMainParams);
    bool trvpFed = FedSince(TaskTRVPId, TASK_START_TICK);
    bool mainFed = FedSince(TaskMainId, TASK_START_TICK);
    vt_check("W5e", "-", trvpFed && mainFed,
             "une periode sans message -> signe de vie de TRVP : %s, de TaskMain : %s (attendu oui, oui)",
             trvpFed ? "oui" : "non", mainFed ? "oui" : "non");
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
    RunTaskOnce(TaskWDG, &sWdgParams);
    vTestSetDelayCallsBeforeEscape(0, NULL);

    vt_check("W6a", "-", sRefreshCount == 3,
             "3 periodes avec trafic (12 signes de vie des taches rapides, puis 1 par tache lente) -> IWDG rafraichi "
             "%u fois (attendu 3 : aucune tache lente ne doit paraitre en retard)",
             sRefreshCount);
}

// Ordres HBLed deposes par TaskWDG dans la queue de TaskIHM : leur nombre, et l'etat du premier et du dernier
static unsigned DrainHeartbeat(bool *first, bool *last)
{
    unsigned count = 0U;
    IHMOrder_t order;
    while (xQueueReceive(xIHMQueue, &order, 0) == pdPASS)
    {
        if (order.order == HBLed)
        {
            *first = (count == 0U) ? order.state : *first;
            *last = order.state;
            count++;
        }
    }
    return count;
}

// T24 (C7, decide le 01/11/2026, revisable) : la LED de vie bascule toutes les HB_LED_HALF_PERIOD_MS, par la queue de
// TaskIHM, seulement quand toutes les taches sont a l'heure, et sans que TaskWDG attende
static void TestWatchdog_Heartbeat(void)
{
    const unsigned periods = (unsigned)(2U * HB_LED_HALF_PERIOD_MS / WDG_PERIOD_MS); // 1 s
    bool first = false;
    bool last = true;

    // T24a : tout va bien pendant 1 s -> deux bascules, ON puis OFF
    Setup();
    sFeedUntil = 0xFFFFFFFFu;
    vTestSetDelayCallsBeforeEscape(periods, FeedCanRx);
    RunTaskOnce(TaskWDG, &sWdgParams);
    vTestSetDelayCallsBeforeEscape(0, NULL);
    unsigned toggles = DrainHeartbeat(&first, &last);
    vt_check("T24a", "-", (toggles == 2U) && first && !last,
             "%u periodes de %lu ms sans retard -> %u ordre(s) HBLed, premier %s, dernier %s (attendu 2 : ON puis OFF, "
             "une bascule toutes les %u ms)",
             periods, (unsigned long)WDG_PERIOD_MS, toggles, first ? "ON" : "OFF", last ? "ON" : "OFF",
             (unsigned)HB_LED_HALF_PERIOD_MS);

    // T24b : TaskCanRx n'a jamais donne signe de vie et le demarrage est loin -> aucune bascule
    Setup();
    vTestSetTickCount(2000U);
    sFeedUntil = 0U;
    vTestSetDelayCallsBeforeEscape(periods, FeedCanRx);
    RunTaskOnce(TaskWDG, &sWdgParams);
    vTestSetDelayCallsBeforeEscape(0, NULL);
    unsigned lateToggles = DrainHeartbeat(&first, &last);
    vt_check("T24b", "-", (lateToggles == 0U) && (sRefreshCount == 0U),
             "TaskCanRx en retard pendant 1 s -> %u ordre(s) HBLed, IWDG rafraichi %u fois (attendu 0 et 0 : la LED ne "
             "dit plus que tout va bien)",
             lateToggles, sRefreshCount);

    // T24c : queue de TaskIHM pleine -> TaskWDG essaie sans attendre, et rafraichit toujours l'IWDG
    Setup();
    sFeedUntil = 0xFFFFFFFFu;
    IHMOrder_t filler = {.order = FaultLight, .state = false};
    while (xQueueSend(xIHMQueue, &filler, 0) == pdPASS)
    {
    }
    vTestResetFullSends();
    vTestSetDelayCallsBeforeEscape(periods, FeedCanRx);
    RunTaskOnce(TaskWDG, &sWdgParams);
    vTestSetDelayCallsBeforeEscape(0, NULL);
    unsigned refused = uxTestFullSendCount();
    TickType_t wait = xTestMaxWaitOnFullSend();
    vt_check("T24c", "-", (refused > 0U) && (wait == 0U) && (sRefreshCount == periods),
             "queue de TaskIHM pleine pendant %u periodes -> %u envoi(s) de la LED refuse(s), attente demandee %lu ms, "
             "IWDG rafraichi %u fois (attendu au moins 1, 0 ms et %u)",
             periods, refused, (unsigned long)wait, sRefreshCount, periods);
}

int main(void)
{
    vt_begin("Watchdog");

    TestWatchdog_TableSize();
    TestWatchdog_Registration();
    TestWatchdog_RefreshWhenHealthy();
    TestWatchdog_NoRefreshWhenStalled();
    TestWatchdog_CanRxWithoutTraffic();
    TestWatchdog_OutputTasksWithoutMessages();
    TestWatchdog_TrafficKeepsSlowTasks();
    TestWatchdog_Heartbeat();

    return vt_end();
}
