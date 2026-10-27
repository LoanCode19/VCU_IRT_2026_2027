// Secteur : conformite au reglement Formula Student 2026 (FS-Rules_2026_elec.pdf, v1.0)
//
// Un test par exigence du reglement qui depend du LOGICIEL du VCU. L'identifiant est l'article.
// Les exigences materielles (SDC, BSPD, TSAL cable, IMD...) ou dont le responsable n'est pas
// encore defini (VCU, onduleur ou AMS ?) sont listees dans tests/REGLEMENT_FS.md.
//
// Pour ne pas dependre du bug B2 (TRVP et Main n'utilisent pas le meme ID), les tests de
// TaskMain lui envoient directement processedThrottleId, l'identifiant qu'elle attend.

#include <stdbool.h>
#include <stdint.h>

#include "structs.h"
#include "queueHandles.h"
#include "throttle.h"
#include "utils.h"
#include "my_fp.h"
#include "TaskMain.h"
#include "TaskRawValuesProcessing.h"
#include "TaskIHM.h"
#include "TaskCoolingSystem.h"
#include "test_app.h"
#include "test_vehicle.h"
#include "vcu_test.h"

#define ADC_MAX 4095        // CAN 12 bits du STM32F407
#define T_NOW 5000U         // ms
#define SCS_MAX_DELAY 500U  // T 11.9.4 : retard maximal d'un message de signal critique

extern float SpeedFiltered;
extern float throttleRamped;

static MainParams_t sMainParams;
static TRVPParams_t sTrvpParams;
static TaskIHMParam_t sIhmParams;

// ----------------------------------------------------------------------------------------------
// Outils

static void Setup(void)
{
    BootMainApp(); // queues comme sur la carte + initThrottleValues()
    sMainParams = (MainParams_t){&xMainQueue, &xTRVPQueue, &xIHMQueue, &xCanTxQueue, TaskMainId};
    sTrvpParams = (TRVPParams_t){&xTRVPQueue, &xMainQueue, TaskTRVPId};
    sIhmParams = (TaskIHMParam_t){&xIHMQueue, &xDSQueue, TaskIHMId};
    SpeedFiltered = 0.0f;
    throttleRamped = 0.0f;
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(T_NOW);
}

static int32_t Raw(int potIdx, float percent)
{
    return (int32_t)(potmin[potIdx] + (potmax[potIdx] - potmin[potIdx]) * percent / 100.0f);
}

static dataMessage_t Apps(int potIdx, int32_t raw, DataTimeStatus_t status)
{
    dataMessage_t msg = {.id = (potIdx == 0) ? apps1Id : apps2Id, .data = raw, .timeStatus = status};
    return msg;
}

static float Command(dataMessage_t apps1, dataMessage_t apps2)
{
    return GetUserThrottleCommand(&apps1, &apps2, 0, false);
}

static void Push(QueueHandle_t queue, MessageIds id, int32_t data, TickType_t timestamp)
{
    dataMessage_t msg = {.id = id, .data = data, .timestamp = timestamp, .timeStatus = STATUS_TIME_OK};
    (void)xQueueSend(queue, &msg, 0);
}

// Donnees de Main : consigne pedale deja calculee, vitesse, temperatures et tension fraiches
static void PushMainData(float pedalPercent, TickType_t commandTimestamp)
{
    Push(xMainQueue, processedThrottleId, FP_FROMFLT(pedalPercent), commandTimestamp);
    Push(xMainQueue, calculatedSpeedId, 0, T_NOW);
    Push(xMainQueue, motor_tempId, 40, T_NOW);
    Push(xMainQueue, inverter_tempId, 40, T_NOW);
    Push(xMainQueue, voltageId, 350, T_NOW);
}

// EV 4.11.8 : sequence R2D, puis le SDC s'ouvre a la passe TEST_R2D_PASSES
static void SdcOpensInR2D(unsigned pass, TickType_t now)
{
    if (pass < TEST_R2D_PASSES)
    {
        TestR2DEntry(pass, now);
    }
    else
    {
        TestPushVehicle((TestVehicle_t){true, false, false, true}, now);
    }
}

// TaskMain avec une consigne pedale deja calculee, pendant la sequence R2D (T8) si enterR2D, sinon TS actif sans
// sequence. Renvoie la consigne onduleur de la derniere passe.
static bool RunMain(float pedalPercent, TickType_t commandTimestamp, bool enterR2D, float *torque)
{
    Setup();
    PushMainData(pedalPercent, commandTimestamp);
    TestRunMain(&sMainParams, TEST_R2D_PASSES, enterR2D ? TestR2DEntry : TestTsOnly);
    return TestLastTorque(torque);
}

// Un appel de ProcessThrottle() a l'arret, temperatures et tension nominales, elapsedMs apres le precedent
static float ProcessAtRest(float request, uint32_t elapsedMs)
{
    return ProcessThrottle(request, 0.0f, 40, 40, 350.0f, elapsedMs);
}

// ----------------------------------------------------------------------------------------------
// T 11.8 : capteurs de position de la pedale d'accelerateur (APPS)

static void Test_T11_8(void)
{
    float torque;

    // T 11.8.6 : fonctions de transfert differentes et qui ne se croisent pas
    bool differentAndNonIntersecting = true;
    for (int p = 0; p <= 100; p++)
    {
        if (Raw(0, (float)p) == Raw(1, (float)p))
        {
            differentAndNonIntersecting = false;
        }
    }
    vt_check("FS-1", "T 11.8.6", differentAndNonIntersecting,
             "calibration APPS1 %d->%d, APPS2 %d->%d (attendu : fonctions de transfert differentes, sans point commun)",
             potmin[0], potmax[0], potmin[1], potmax[1]);

    // T 11.8.6 : un court-circuit entre les deux fils de signal doit donner une incoherence
    torque = Command(Apps(0, 2000, STATUS_TIME_OK), Apps(1, 2000, STATUS_TIME_OK));
    vt_check("FS-2", "T 11.8.6", torque <= 0.0f,
             "fils APPS1 et APPS2 en court-circuit (meme tension, ADC 2000) -> couple %.1f %% (attendu 0)", torque);

    // T 11.8.8 / T 11.8.9 : ecart > 10 points -> puissance coupee completement
    torque = Command(Apps(0, Raw(0, 30.0f), STATUS_TIME_OK), Apps(1, Raw(1, 45.0f), STATUS_TIME_OK));
    vt_check("FS-3", "T 11.8.8", torque <= 0.0f,
             "APPS 30 %% / 45 %% (ecart 15 pts) -> couple %.1f %% (attendu 0, pas de mode degrade)", torque);

    torque = Command(Apps(0, Raw(0, 40.0f), STATUS_TIME_OK), Apps(1, Raw(1, 50.0f), STATUS_TIME_OK));
    vt_check("FS-4", "T 11.8.9", torque > 30.0f,
             "APPS 40 %% / 50 %% (ecart 10 pts, autorise) -> couple %.1f %% (attendu > 0)", torque);

    // T 11.8.9 : toute defaillance T 11.9 d'un APPS est une incoherence
    torque = Command(Apps(0, Raw(0, 80.0f), STATUS_TIME_OK), Apps(1, Raw(1, 80.0f), STATUS_TIMEOUT));
    vt_check("FS-5", "T 11.8.9", torque <= 0.0f,
             "APPS2 en timeout, APPS1 80 %% -> couple %.1f %% (attendu 0, pas de capteur unique)", torque);

    // T 11.8.12 : pedale relachee -> couple roue <= 0
    torque = Command(Apps(0, Raw(0, 0.0f), STATUS_TIME_OK), Apps(1, Raw(1, 0.0f), STATUS_TIME_OK));
    vt_check("FS-6", "T 11.8.12", torque <= 0.0f, "pedale relachee -> couple %.1f %% (attendu <= 0)", torque);
}

// ----------------------------------------------------------------------------------------------
// T 11.9 : signaux critiques (SCS)

static void Test_T11_9(void)
{
    float torque;

    // T 11.9.2.a : fil coupe (entree tiree a 0 V)
    torque = Command(Apps(0, 0, STATUS_TIME_OK), Apps(1, Raw(1, 50.0f), STATUS_TIME_OK));
    vt_check("FS-7", "T 11.9.2.a", torque <= 0.0f,
             "fil APPS1 coupe (ADC 0), APPS2 50 %% -> couple %.1f %% (attendu 0)", torque);

    // T 11.9.2.b : court-circuit a la tension d'alimentation
    torque = Command(Apps(0, ADC_MAX, STATUS_TIME_OK), Apps(1, Raw(1, 95.0f), STATUS_TIME_OK));
    vt_check("FS-8", "T 11.9.2.b", torque <= 0.0f,
             "APPS1 court-circuite au + (ADC %d), APPS2 95 %% -> couple %.1f %% (attendu 0)", ADC_MAX, torque);

    // T 11.9.2.c : une valeur hors plage doit etre detectable sur toute l'echelle de l'ADC
    for (int idx = 0; idx < 2; idx++)
    {
        int detectable = 0;
        for (int raw = 0; raw <= ADC_MAX; raw++)
        {
            if (!AppsInRange(raw, idx))
            {
                detectable++;
            }
        }
        vt_check(idx == 0 ? "FS-9a" : "FS-9b", "T 11.9.2.c", detectable > 0,
                 "APPS%d : %d valeur(s) ADC sur 0..%d detectee(s) hors plage (attendu > 0 ; fenetre actuelle "
                 "%d..%d avec POT_SLACK)",
                 idx + 1, detectable, ADC_MAX, potmin[idx] - 200, potmax[idx] + 200);
    }

    // T 11.9.2.d / T 11.9.4 : APPS vieux de plus de 500 ms -> pas de couple (dans TaskRawValuesProcessing)
    Setup();
    Push(xTRVPQueue, apps1Id, Raw(0, 40.0f), T_NOW - (SCS_MAX_DELAY + 100U));
    Push(xTRVPQueue, apps2Id, Raw(1, 40.0f), T_NOW - (SCS_MAX_DELAY + 100U));
    RunTaskOnce(TaskRawValuesProcessing, &sTrvpParams);
    dataMessage_t out = {0};
    float pedal = -1.0f;
    while (xQueueReceive(xMainQueue, &out, 0) == pdPASS)
    {
        if (out.id == processedThrottleId)
        {
            pedal = FP_TOFLOAT(out.data);
        }
    }
    vt_check("FS-10", "T 11.9.4", pedal == 0.0f,
             "TRVP : APPS recus il y a 600 ms -> consigne pedale %.1f %% (attendu 0)", pedal);

    // T 11.9.4 : consigne vieille de plus de 500 ms -> pas de couple (dans TaskMain)
    float fresh = 0.0f;
    float stale = 0.0f;
    bool sentFresh = RunMain(40.0f, T_NOW, true, &fresh);
    if (!sentFresh || fresh <= 0.0f)
    {
        vt_blocked("FS-11", "T 11.9.4", "TaskMain ne produit aucun couple meme avec une consigne fraiche");
    }
    else
    {
        bool sentStale = RunMain(40.0f, T_NOW - (SCS_MAX_DELAY + 100U), true, &stale);
        vt_check("FS-11", "T 11.9.4", !sentStale || stale <= 0.0f,
                 "Main : consigne pedale vieille de 600 ms -> couple %.1f %% (attendu 0 ; consigne fraiche : %.1f %%)",
                 stale, fresh);
    }
}

// ----------------------------------------------------------------------------------------------
// EV 2.2 : limitation de puissance, D 2.3.2 : pas de marche arriere

static void Test_EV2_2(void)
{
    vt_blocked("FS-12", "EV 2.2.1/2",
               "80 kW / 500 A : pas de mesure Idc dans le VCU. A decider : limite faite par le VCU ou par l'onduleur ?");

    initThrottleValues();
    throttleRamped = 0.0f;
    float torque = 0.0f;
    for (int i = 0; i < 100; i++)
    {
        torque = ProcessAtRest(-50.0f, 10U);
    }
    vt_check("FS-13", "EV 2.2.4", torque >= 0.0f,
             "consigne -50 %% a l'arret -> couple %+.1f %% (attendu >= 0 : roues jamais en arriere, cf. D 2.3.2)", torque);
}

// ----------------------------------------------------------------------------------------------
// T 6.1.13 : freinage par recuperation

static void Test_T6(void)
{
    initThrottleValues();
    SpeedFiltered = 0.0f;
    for (int i = 0; i < 200; i++)
    {
        (void)CalcThrottle(0.0f, 1500, false);
    }
    float torque = CalcThrottle(0.0f, 1500, true);
    vt_check("FS-14", "T 6.1.13", torque <= 0.0f,
             "pedale de frein appuyee en roulant -> couple moteur %+.1f %% (attendu <= 0 : recuperation = couple negatif)",
             torque);
}

// ----------------------------------------------------------------------------------------------
// EV 4.11 / EV 4.12 : Ready-to-Drive

static void Test_EV4_11(void)
{
    float torque = 0.0f;
    bool sent = RunMain(40.0f, T_NOW, false, &torque);
    vt_check("FS-15", "EV 4.11.7", !sent || torque <= 0.0f,
             "TS actif, pedale 40 %% sans sequence R2D (frein + interrupteur) -> couple %.1f %% (attendu 0)", torque);

    // EV 4.11.8 : en R2D, le SDC s'ouvre -> plus de couple a la periode ou Main le voit
    Setup();
    PushMainData(40.0f, T_NOW);
    TestRunMain(&sMainParams, TEST_R2D_PASSES + 1U, SdcOpensInR2D);
    unsigned opened = TEST_R2D_PASSES;
    float inR2D = sTestRun.torque[opened - 1U];
    if (!sTestRun.torqueSent[opened - 1U] || (inR2D <= 0.0f))
    {
        vt_blocked("FS-16", "EV 4.11.8", "pas de couple apres la sequence R2D : sortie a l'ouverture du SDC non verifiable");
    }
    else
    {
        float afterOpen = sTestRun.torqueSent[opened] ? sTestRun.torque[opened] : 0.0f;
        vt_check("FS-16", "EV 4.11.8", afterOpen <= 0.0f,
                 "en R2D, consigne %.1f %% ; le SDC s'ouvre -> consigne %.1f %% a la meme periode (attendu 0)", inR2D,
                 afterOpen);
    }

    // EV 4.12.1 : le buzzer R2D doit pouvoir etre commande (condition necessaire du son de 1 a 3 s)
    Setup();
    IHMOrder_t order = {.order = R2Dbuzzer, .state = true};
    (void)xQueueSend(xIHMQueue, &order, 0);
    RunTaskOnce(TaskIHM, &sIhmParams);
    DigitalOrder_t output = {0};
    bool received = xQueueReceive(xDSQueue, &output, 0) == pdPASS;
    vt_check("FS-17", "EV 4.12.1", received && output.order == R2Dbuzzer && output.state,
             "ordre buzzer R2D -> sortie recoit order=%d (attendu %d) : sans ca, pas de son R2D",
             received ? (int)output.order : -1, (int)R2Dbuzzer);
}

// ----------------------------------------------------------------------------------------------
// IN 9.2.1 : ventilateurs allumes pendant le test de pluie (voiture froide, TS actif, pas en R2D : IN 9.2.2, 9.2.4)

// Le refroidissement, voiture froide (20 degC), recoit "TS actif" a la valeur tsActive, date de tsTimestamp.
// Renvoie le dernier ordre des ventilateurs.
static bool CoolingFansWhenCold(int32_t tsActive, TickType_t tsTimestamp)
{
    Setup();
    QueueHandle_t coolingQueue = xQueueCreate(8U, sizeof(dataMessage_t));
    CoolingParams_t coolingParams = {&coolingQueue, TaskCoolingId};
    Push(coolingQueue, motor_tempId, 20, T_NOW);
    Push(coolingQueue, inverter_tempId, 20, T_NOW);
    Push(coolingQueue, tsActiveId, tsActive, tsTimestamp);
    RunTaskOnce(TaskCoolingSystem, &coolingParams);

    bool fansOn = false;
    DigitalOrder_t output = {0};
    while (xQueueReceive(xDSQueue, &output, 0) == pdPASS)
    {
        if (output.order == CoolingFans)
        {
            fansOn = output.state;
        }
    }
    return fansOn;
}

static void Test_IN9(void)
{
    bool fansOn = CoolingFansWhenCold(1, T_NOW);
    vt_check("FS-18", "IN 9.2.1", fansOn,
             "TS actif, voiture froide (20 degC) -> ventilateurs %s (attendu ON pour le test de pluie)",
             fansOn ? "ON" : "OFF, aucune commande pour les forcer");

    // S12a (T9) : signal TS perdu -> ventilateurs ON, comme une temperature perdue
    bool fansOnLost = CoolingFansWhenCold(0, T_NOW - 200U);
    vt_check("S12a", "IN 9.2.1", fansOnLost,
             "signal TS perdu (dernier message vieux de 200 ms), voiture froide -> ventilateurs %s (attendu ON, T9)",
             fansOnLost ? "ON" : "OFF");
}

int main(void)
{
    vt_begin("Reglement FS 2026");
    initThrottleValues(); // calibration APPS utilisee par T 11.8 / T 11.9

    Test_T11_8();
    Test_T11_9();
    Test_EV2_2();
    Test_T6();
    Test_EV4_11();
    Test_IN9();

    return vt_end();
}
