// Secteur : IHM et sorties tout-ou-rien (buzzer R2D, voyant defaut, LED de vie, pompe et ventilateurs)
// Code teste : TaskIHM (src/TaskIHM.c) et TaskCoolingSystem (src/TaskCoolingSystem.c) avec les queues de MainApp()
// Problemes : B7 de AUDIT_VCU.md ; regles du refroidissement (project_config.h) que le test historique ne couvre pas

#include <stdbool.h>
#include <string.h>

#include "structs.h"
#include "queueHandles.h"
#include "TaskIHM.h"
#include "TaskCoolingSystem.h"
#include "TaskDigitalSignals.h"
#include "test_app.h"
#include "vcu_test.h"

static TaskIHMParam_t sIhmParams;

// Envoie un ordre a TaskIHM et renvoie l'ordre recu par TaskDigitalSignals
static bool Forward(Order_t order, bool state, DigitalOrder_t *out)
{
    BootMainApp();
    sIhmParams = (TaskIHMParam_t){&xIHMQueue, &xDSQueue, TaskIHMId};
    IHMOrder_t in = {.order = order, .state = state};
    (void)xQueueSend(xIHMQueue, &in, 0);
    RunTaskOnce(TaskIHM, &sIhmParams);
    return xQueueReceive(xDSQueue, out, 0) == pdPASS;
}

static void CheckForward(const char *id, const char *rule, Order_t order, const char *name)
{
    DigitalOrder_t out = {0};
    bool received = Forward(order, true, &out);
    vt_check(id, rule, received && (out.order == order) && out.state,
             "ordre IHM %s ON -> sortie recoit order=%d state=%d (attendu order=%d state=1)",
             name, received ? (int)out.order : -1, received ? (int)out.state : -1, (int)order);
}

// ------------------------------------------------------------------------------------------------------------------
// TaskCoolingSystem : les messages sont traites dans l'ordre, et chaque changement d'une sortie donne un ordre. On lit
// la suite des ordres d'une sortie : "1" allume, "0" eteint, dans l'ordre.

#define COOLING_T0 1000U

static CoolingParams_t sCoolingParams;
static TickType_t sCoolingAge = 0U; // age des messages deposes par RunCooling (CL-h : temperature deja perimee)

typedef struct
{
    MessageIds id;
    int32_t value;
} CoolingInput_t;

// Fait tourner la tache sur ces messages, puis pendant waits attentes de COOLING_TIMEOUT sans message. Ecrit dans pump
// et fans la suite des ordres recus (au plus 7 par sortie).
static void RunCooling(const CoolingInput_t *inputs, unsigned count, unsigned waits, char pump[8], char fans[8])
{
    BootMainApp();
    sCoolingParams = (CoolingParams_t){&xCoolingQueue, TaskCoolingId, &xDSQueue};
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(COOLING_T0);
    for (unsigned i = 0; i < count; i++)
    {
        dataMessage_t msg = {.id = inputs[i].id, .data = inputs[i].value, .timestamp = COOLING_T0 - sCoolingAge};
        (void)xQueueSend(xCoolingQueue, &msg, 0);
    }
    vTestSetBlockingTimeoutsBeforeEscape(waits);
    RunTaskOnce(TaskCoolingSystem, &sCoolingParams);
    vTestSetBlockingTimeoutsBeforeEscape(0U);
    unsigned np = 0U;
    unsigned nf = 0U;
    DigitalOrder_t order;
    while (xQueueReceive(xDSQueue, &order, 0) == pdPASS)
    {
        if ((order.order == CoolingPump) && (np < 7U))
        {
            pump[np++] = order.state ? '1' : '0';
        }
        else if ((order.order == CoolingFans) && (nf < 7U))
        {
            fans[nf++] = order.state ? '1' : '0';
        }
    }
    pump[np] = '\0';
    fans[nf] = '\0';
}

// Les scenarios de l'ancien test historique (test_cooling_digital.c), avec le vrai utils.c. Ils gardent des
// comportements du code d'origine, notes "a confirmer" comme CL-a a CL-c (T27, etendu par T45).
static void TestCooling_FirstOrders(void)
{
    char pump[8];
    char fans[8];

    // Premier message chaud : un ordre ON pour chaque sortie, un seul
    static const CoolingInput_t hot[] = {{motor_tempId, 56}};
    RunCooling(hot, 1U, 0U, pump, fans);
    vt_check("CL-e", "-", (strcmp(pump, "1") == 0) && (strcmp(fans, "1") == 0),
             "premier message, moteur a 56 degres -> pompe %s, ventilateurs %s (attendu 1 et 1 : au-dessus de %d et de "
             "%d degres)",
             pump, fans, COOLING_PUMP_ON_TEMP_C, COOLING_FANS_ON_TEMP_C);

    // Premier message froid : les deux ordres partent quand meme, a OFF, pour mettre les sorties dans un etat connu
    static const CoolingInput_t cold[] = {{motor_tempId, 40}};
    RunCooling(cold, 1U, 0U, pump, fans);
    vt_check("CL-f", "-", (strcmp(pump, "0") == 0) && (strcmp(fans, "0") == 0),
             "premier message, moteur a 40 degres -> pompe %s, ventilateurs %s (attendu 0 et 0 : les premiers ordres "
             "partent meme a OFF)",
             pump, fans);

    // Deux messages chauds de suite : un ordre inchange ne repart pas
    static const CoolingInput_t hotTwice[] = {{motor_tempId, 56}, {motor_tempId, 57}};
    RunCooling(hotTwice, 2U, 0U, pump, fans);
    vt_check("CL-g", "-", (strcmp(pump, "1") == 0) && (strcmp(fans, "1") == 0),
             "moteur a 56 puis 57 degres -> pompe %s, ventilateurs %s (attendu 1 et 1 : pas de second envoi)", pump,
             fans);

    // Temperature deja perimee a son arrivee (plus vieille que TEMP_TIMEOUT_MS) : perdue, tout s'allume, meme a 30
    sCoolingAge = (TickType_t)(TEMP_TIMEOUT_MS + 100);
    static const CoolingInput_t stale[] = {{motor_tempId, 30}};
    RunCooling(stale, 1U, 0U, pump, fans);
    sCoolingAge = 0U;
    vt_check("CL-h", "-", (strcmp(pump, "1") == 0) && (strcmp(fans, "1") == 0),
             "moteur a 30 degres, message vieux de %d ms -> pompe %s, ventilateurs %s (attendu 1 et 1 : une temperature "
             "perdue allume tout)",
             TEMP_TIMEOUT_MS + 100, pump, fans);
}

// CT3 (T9) : TS recu a 0 (TS coupe), voiture froide : les temperatures decident, tout reste eteint. Les autres tests
// essaient TS actif (FS-18), TS perdu (S12a) et TS jamais recu, pas celui-ci.
// CT5 : un message que le refroidissement n'attend pas (ici la tension) est ignore.
static void TestCooling_TsOffAndUnknown(void)
{
    char pump[8];
    char fans[8];

    static const CoolingInput_t tsOff[] = {{tsActiveId, 0}, {motor_tempId, 20}, {inverter_tempId, 20}};
    RunCooling(tsOff, 3U, 0U, pump, fans);
    vt_check("CT3a", "-", (strcmp(pump, "0") == 0) && (strcmp(fans, "0") == 0),
             "TS recu a 0, moteur et onduleur a 20 degres -> pompe %s, ventilateurs %s (attendu 0 et 0 : TS coupe, les "
             "temperatures decident, T9)",
             pump, fans);

    static const CoolingInput_t unknown[] = {{motor_tempId, 20}, {voltageId, 80}};
    RunCooling(unknown, 2U, 0U, pump, fans);
    vt_check("CT5c", "-", (strcmp(pump, "0") == 0) && (strcmp(fans, "0") == 0),
             "moteur a 20 degres, puis un message de tension (80) que la tache n'attend pas -> pompe %s, ventilateurs "
             "%s (attendu 0 et 0 : message ignore)",
             pump, fans);
}

// TaskDigitalSignals retient l'etat demande de chaque sortie (les broches viendront avec D5)
extern volatile bool digitalOutputs[OUTPUT_COUNT];
static DigitalSignalsParams_t sDigitalParams;

static void TestDigital_Outputs(void)
{
    BootMainApp();
    sDigitalParams = (DigitalSignalsParams_t){&xDSQueue, TaskDigitalSignalsId};
    const DigitalOrder_t orders[] = {{CoolingPump, true}, {FaultLight, true}, {CoolingPump, false}};
    for (unsigned i = 0; i < sizeof(orders) / sizeof(orders[0]); i++)
    {
        (void)xQueueSend(xDSQueue, &orders[i], 0);
    }
    RunTaskOnce(TaskDigitalSignals, &sDigitalParams);
    bool pump = digitalOutputs[CoolingPump];
    bool light = digitalOutputs[FaultLight];
    vt_check("DS-a", "-", !pump && light,
             "ordres pompe ON, voyant ON, pompe OFF -> pompe %s, voyant %s (attendu OFF, ON : le dernier ordre de chaque "
             "sortie)",
             pump ? "ON" : "OFF", light ? "ON" : "OFF");
}

// CT4 : un ordre de sortie hors du tableau (OUTPUT_COUNT, au-dela, ou negatif) ne change aucune sortie. Comme pour
// wdgFeed(), une ecriture juste a cote du tableau ne se voit pas a coup sur : on verifie qu'aucune sortie valide ne bouge.
static void TestDigital_OrderOutOfRange(void)
{
    BootMainApp();
    sDigitalParams = (DigitalSignalsParams_t){&xDSQueue, TaskDigitalSignalsId};
    for (unsigned i = 0; i < (unsigned)OUTPUT_COUNT; i++)
    {
        digitalOutputs[i] = false;
    }
    const DigitalOrder_t orders[] = {{(Order_t)OUTPUT_COUNT, true}, {(Order_t)(OUTPUT_COUNT + 1), true}, {(Order_t)-1, true}};
    for (unsigned i = 0; i < sizeof(orders) / sizeof(orders[0]); i++)
    {
        (void)xQueueSend(xDSQueue, &orders[i], 0);
    }
    RunTaskOnce(TaskDigitalSignals, &sDigitalParams);
    unsigned on = 0U;
    for (unsigned i = 0; i < (unsigned)OUTPUT_COUNT; i++)
    {
        on += digitalOutputs[i] ? 1U : 0U;
    }
    unsigned left = (unsigned)uxTestQueueCount(xDSQueue);
    vt_check("CT4b", "-", (on == 0U) && (left == 0U),
             "ordres ON pour les sorties %d, %d et -1 (hors du tableau) -> %u sortie(s) allumee(s), %u ordre(s) restes "
             "dans la queue (attendu 0 et 0 : ordres lus et ignores)",
             (int)OUTPUT_COUNT, (int)OUTPUT_COUNT + 1, on, left);
}

// CT4 : sans queue des sorties (le parametre pointe vers une queue nulle), le refroidissement n'envoie rien vers une
// queue nulle : la vraie FreeRTOS s'y arreterait (configASSERT).
static void TestCooling_NoOutputQueue(void)
{
    BootMainApp();
    static QueueHandle_t noOutputs = NULL;
    sCoolingParams = (CoolingParams_t){&xCoolingQueue, TaskCoolingId, &noOutputs};
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(COOLING_T0);
    unsigned before = uxTestNullQueueSends();
    dataMessage_t msg = {.id = motor_tempId, .data = 60, .timestamp = COOLING_T0};
    (void)xQueueSend(xCoolingQueue, &msg, 0);
    RunTaskOnce(TaskCoolingSystem, &sCoolingParams);
    unsigned nullSends = uxTestNullQueueSends() - before;
    vt_check("CT4h", "-", nullSends == 0U,
             "refroidissement sans queue des sorties (pointeur vers une queue nulle), moteur a 60 degres -> %u envoi(s) "
             "vers une queue nulle (attendu 0)",
             nullSends);
}

// Q9 (T28) : les ordres partent dans la queue recue en parametre. La globale xDSQueue est une autre queue : elle ne
// doit rien recevoir.
static void TestCooling_QueueFromParameters(void)
{
    BootMainApp();
    QueueHandle_t ownDS = xQueueCreate(8U, sizeof(DigitalOrder_t));
    sCoolingParams = (CoolingParams_t){&xCoolingQueue, TaskCoolingId, &ownDS};
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(COOLING_T0);
    dataMessage_t msg = {.id = motor_tempId, .data = 20, .timestamp = COOLING_T0};
    (void)xQueueSend(xCoolingQueue, &msg, 0);
    RunTaskOnce(TaskCoolingSystem, &sCoolingParams);
    unsigned inOwn = (unsigned)uxTestQueueCount(ownDS);
    unsigned inGlobal = (unsigned)uxTestQueueCount(xDSQueue);
    vt_check("Q9b", "-", (inOwn == 2U) && (inGlobal == 0U),
             "temperature recue -> %u ordre(s) dans la queue des parametres, %u dans la globale xDSQueue (attendu 2 "
             "et 0)",
             inOwn, inGlobal);
}

static void TestCooling_Rules(void)
{
    char pump[8];
    char fans[8];
    char pump2[8];
    char fans2[8];
    char pump3[8];
    char fans3[8];

    // Hysteresis de la pompe : ON a 50 degres, OFF a 45, et entre les deux elle garde son etat
    static const CoolingInput_t inBand[] = {{motor_tempId, 52}, {motor_tempId, 47}, {motor_tempId, 52}};
    static const CoolingInput_t atOff[] = {{motor_tempId, 52}, {motor_tempId, COOLING_PUMP_OFF_TEMP_C}, {motor_tempId, 52}};
    static const CoolingInput_t atOn[] = {{motor_tempId, 40}, {motor_tempId, COOLING_PUMP_ON_TEMP_C}};
    RunCooling(inBand, 3U, 0U, pump, fans);
    RunCooling(atOff, 3U, 0U, pump2, fans2);
    RunCooling(atOn, 2U, 0U, pump3, fans3);
    vt_check("CL-a", "-", (strcmp(pump, "1") == 0) && (strcmp(pump2, "101") == 0) && (strcmp(pump3, "01") == 0),
             "pompe, moteur 52 / 47 / 52 degres -> ordres %s ; 52 / %d / 52 -> %s ; 40 / %d -> %s (attendu 1, 101, 01 : "
             "ON a %d, OFF a %d, etat garde entre les deux)",
             pump, COOLING_PUMP_OFF_TEMP_C, pump2, COOLING_PUMP_ON_TEMP_C, pump3, COOLING_PUMP_ON_TEMP_C,
             COOLING_PUMP_OFF_TEMP_C);

    // Les ventilateurs ont leurs propres seuils, plus hauts : a 52 degres, pompe ON, ventilateurs OFF
    static const CoolingInput_t warm[] = {{motor_tempId, 52}};
    RunCooling(warm, 1U, 0U, pump, fans);
    vt_check("CL-b", "-", (strcmp(pump, "1") == 0) && (strcmp(fans, "0") == 0),
             "moteur a 52 degres -> pompe %s, ventilateurs %s (attendu 1 et 0 : ventilateurs a partir de %d)", pump,
             fans, COOLING_FANS_ON_TEMP_C);

    // La plus chaude des deux temperatures decide ; une seule recue suffit
    static const CoolingInput_t hotInverter[] = {{motor_tempId, 40}, {inverter_tempId, 52}};
    static const CoolingInput_t inverterOnly[] = {{inverter_tempId, 52}};
    RunCooling(hotInverter, 2U, 0U, pump, fans);
    RunCooling(inverterOnly, 1U, 0U, pump2, fans2);
    vt_check("CL-c", "-", (pump[strlen(pump) - 1U] == '1') && (strcmp(pump2, "1") == 0),
             "moteur 40, onduleur 52 degres -> pompe %s ; onduleur seul a 52 -> %s (attendu derniere commande 1, puis 1 "
             ": la plus chaude des temperatures valides)",
             pump, pump2);

    // Temperature de l'onduleur perdue (plus de 500 ms), moteur jamais recu : tout s'allume
    static const CoolingInput_t inverterLost[] = {{inverter_tempId, 40}};
    unsigned waits = (unsigned)(TEMP_TIMEOUT_MS / COOLING_TIMEOUT) + 2U;
    RunCooling(inverterLost, 1U, waits, pump, fans);
    vt_check("CL-d", "-", (strcmp(pump, "01") == 0) && (strcmp(fans, "01") == 0),
             "onduleur a 40 degres puis plus rien pendant %u ms, moteur jamais recu -> pompe %s, ventilateurs %s "
             "(attendu 01 et 01 : une temperature perdue allume tout)",
             waits * (unsigned)COOLING_TIMEOUT, pump, fans);
}

int main(void)
{
    vt_begin("IHM et sorties TOR");

    CheckForward("B7a", "EV 4.12.1", R2Dbuzzer, "R2Dbuzzer");
    CheckForward("B7b", "T 11.9.5", FaultLight, "FaultLight");
    CheckForward("B7c", "-", HBLed, "HBLed");

    // Un ordre OFF passe aussi : le voyant defaut doit pouvoir s'eteindre
    DigitalOrder_t off = {0};
    bool received = Forward(FaultLight, false, &off);
    vt_check("B7d", "-", received && (off.order == FaultLight) && !off.state,
             "ordre IHM FaultLight OFF -> sortie recoit order=%d state=%d (attendu order=%d state=0)",
             received ? (int)off.order : -1, received ? (int)off.state : -1, (int)FaultLight);

    TestCooling_Rules();
    TestCooling_FirstOrders();
    TestCooling_TsOffAndUnknown();
    TestCooling_QueueFromParameters();
    TestDigital_Outputs();
    TestDigital_OrderOutOfRange();
    TestCooling_NoOutputQueue();

    return vt_end();
}
