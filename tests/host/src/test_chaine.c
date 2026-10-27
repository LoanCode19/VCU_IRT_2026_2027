// Secteur : chaine de couple TaskRawValuesProcessing -> TaskMain -> queue CanTx
// Code teste : les deux taches en vrai, avec les queues de MainApp()
// Problemes : B2, S5, S3 de AUDIT_VCU.md
//
// Les tests de securite ne concluent que si la chaine sait produire du couple (B2a).
// Sinon ils sont BLOQUES : "le couple est a 0" serait vrai pour une mauvaise raison.

#include <stdbool.h>
#include <stdint.h>

#include "structs.h"
#include "queueHandles.h"
#include "throttle.h"
#include "my_fp.h"
#include "TaskMain.h"
#include "TaskRawValuesProcessing.h"
#include "test_app.h"
#include "test_vehicle.h"
#include "vcu_test.h"

#define T_NOW 3000U // ms, assez loin de 0 pour dater des messages "vieux"

extern float SpeedFiltered;
extern float throttleRamped;

static TRVPParams_t sTrvpParams;
static MainParams_t sMainParams;

typedef struct
{
    float appsPercent;
    bool sendTemperatures;
    TickType_t temperatureTimestamp;
    TickType_t voltageTimestamp;
    int32_t wheelRaw; // valeur brute des 4 capteurs de roue
    bool withoutR2D;  // TS actif, mais pas de sequence R2D (S3a)
} ChainInputs_t;

static void Setup(void)
{
    BootMainApp(); // appelle aussi initThrottleValues()
    sTrvpParams = (TRVPParams_t){&xTRVPQueue, &xMainQueue, TaskTRVPId};
    sMainParams = (MainParams_t){&xMainQueue, &xTRVPQueue, &xIHMQueue, &xCanTxQueue, TaskMainId};
    SpeedFiltered = 0.0f;
    throttleRamped = 0.0f;
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(T_NOW);
}

static void Push(QueueHandle_t queue, MessageIds id, int32_t data, TickType_t timestamp)
{
    dataMessage_t msg = {.id = id, .data = data, .timestamp = timestamp, .timeStatus = STATUS_TIME_OK};
    (void)xQueueSend(queue, &msg, 0);
}

// Une passe de TRVP, puis Main pendant la sequence R2D (T8). Renvoie false si aucune consigne n'est sortie a la
// derniere passe.
static bool RunChain(ChainInputs_t in, float *torque)
{
    Setup();
    int32_t raw0 = (int32_t)(potmin[0] + (potmax[0] - potmin[0]) * in.appsPercent / 100.0f);
    int32_t raw1 = (int32_t)(potmin[1] + (potmax[1] - potmin[1]) * in.appsPercent / 100.0f);
    Push(xTRVPQueue, apps1Id, raw0, T_NOW);
    Push(xTRVPQueue, apps2Id, raw1, T_NOW);
    Push(xTRVPQueue, wheelFRId, in.wheelRaw, T_NOW);
    Push(xTRVPQueue, wheelFLId, in.wheelRaw, T_NOW);
    Push(xTRVPQueue, wheelRRId, in.wheelRaw, T_NOW);
    Push(xTRVPQueue, wheelRLId, in.wheelRaw, T_NOW);
    Push(xTRVPQueue, brakePressedId, 0, T_NOW); // frein relache
    if (in.sendTemperatures)
    {
        Push(xMainQueue, motor_tempId, 40, in.temperatureTimestamp);
        Push(xMainQueue, inverter_tempId, 40, in.temperatureTimestamp);
        Push(xMainQueue, voltageId, 350, in.voltageTimestamp);
    }

    RunTaskOnce(TaskRawValuesProcessing, &sTrvpParams);
    TestRunMain(&sMainParams, TEST_R2D_PASSES, in.withoutR2D ? TestTsOnly : TestR2DEntry);
    return TestLastTorque(torque);
}

// Valeur brute d'un capteur de roue pour une vitesse en m/s, avec l'hypothese de D7 (tr/min de roue)
static int32_t WheelRawFor(float speedMs)
{
    return (int32_t)(speedMs * 60.0f / (2.0f * 3.14159265f * WHEEL_RADIUS_M) + 0.5f);
}

// R1 : TaskMain pendant 100 ms sans aucun message. Renvoie le plus long ecart entre deux consignes, du debut
// a la fin de la fenetre, d'apres l'heure que Main ecrit dans chaque consigne.
static TickType_t MainSilentMaxGap(unsigned *count)
{
    Setup();
    vTestSetBlockingTimeoutsBeforeEscape(1U); // boucle reveillee par la queue : une attente de 100 ms
    TestRunMain(&sMainParams, 10U, NULL);     // boucle periodique : 10 passages
    vTestSetBlockingTimeoutsBeforeEscape(0U);

    TickType_t previous = T_NOW;
    TickType_t maxGap = 0;
    *count = 0;
    for (unsigned i = 0; i < sTestRun.passes; i++)
    {
        if (sTestRun.torqueSent[i])
        {
            TickType_t gap = sTestRun.torqueTick[i] - previous;
            maxGap = (gap > maxGap) ? gap : maxGap;
            previous = sTestRun.torqueTick[i];
            (*count)++;
        }
    }
    TickType_t end = T_NOW + 100U;
    return ((end > previous) && ((end - previous) > maxGap)) ? (end - previous) : maxGap;
}

// Ordre envoye par Main a l'IHM pour le voyant defaut, apres RunChain : -1 aucun, 0 eteint, 1 allume
static int FaultLightOrder(void)
{
    return TestLastFaultLight();
}

// S2c : TRVP une passe, APPS a 40 %. Frein jamais recu, ou recu (relache) a brakeTimestamp. Renvoie la consigne
// pedale envoyee a Main, -1 si aucune.
static float TrvpPedal(bool sendBrake, TickType_t brakeTimestamp)
{
    Setup();
    Push(xTRVPQueue, apps1Id, (int32_t)(potmin[0] + (potmax[0] - potmin[0]) * 0.4f), T_NOW);
    Push(xTRVPQueue, apps2Id, (int32_t)(potmin[1] + (potmax[1] - potmin[1]) * 0.4f), T_NOW);
    if (sendBrake)
    {
        Push(xTRVPQueue, brakePressedId, 0, brakeTimestamp);
    }
    RunTaskOnce(TaskRawValuesProcessing, &sTrvpParams);

    float pedal = -1.0f;
    dataMessage_t msg = {0};
    while (xQueueReceive(xMainQueue, &msg, 0) == pdPASS)
    {
        if (msg.id == processedThrottleId)
        {
            pedal = FP_TOFLOAT(msg.data);
        }
    }
    return pedal;
}

// S1k : APPS deposes pour TRVP a chaque passe, et derniere faute APPS envoyee a Main
static float sTrvpApps[2];
static int sLastAppsFault;

static void PushTrvpApps(TickType_t now)
{
    for (int i = 0; i < 2; i++)
    {
        Push(xTRVPQueue, (i == 0) ? apps1Id : apps2Id,
             (int32_t)(potmin[i] + (potmax[i] - potmin[i]) * sTrvpApps[i] / 100.0f), now);
    }
}

static void DrainAppsFault(void)
{
    dataMessage_t msg = {0};
    while (xQueueReceive(xMainQueue, &msg, 0) == pdPASS)
    {
        if (msg.id == appsFaultId)
        {
            sLastAppsFault = (int)msg.data;
        }
    }
}

static void TrvpAppsHook(TickType_t now)
{
    DrainAppsFault();
    PushTrvpApps(now);
}

// TRVP pendant passes periodes, APPS a percent1 / percent2. Renvoie la derniere faute APPS envoyee a Main : 1
// confirmee, 0 aucune, -1 rien envoye.
static int TrvpAppsFault(float percent1, float percent2, unsigned passes)
{
    Setup();
    sTrvpApps[0] = percent1;
    sTrvpApps[1] = percent2;
    sLastAppsFault = -1;
    PushTrvpApps(T_NOW);
    vTestSetDelayCallsBeforeEscape(passes - 1U, TrvpAppsHook);
    RunTaskOnce(TaskRawValuesProcessing, &sTrvpParams);
    vTestSetDelayCallsBeforeEscape(0U, NULL);
    DrainAppsFault();
    return sLastAppsFault;
}

// S1l : sequence R2D, donnees fraiches a chaque passe ; a partir de la passe TEST_R2D_PASSES, TRVP signale une faute
// APPS confirmee
static void AppsFaultInR2D(unsigned pass, TickType_t now)
{
    TestR2DEntry(pass, now);
    Push(xMainQueue, processedThrottleId, 0, now);
    Push(xMainQueue, calculatedSpeedId, 0, now);
    Push(xMainQueue, motor_tempId, 40, now);
    Push(xMainQueue, inverter_tempId, 40, now);
    Push(xMainQueue, voltageId, 350, now);
    if (pass >= TEST_R2D_PASSES)
    {
        Push(xMainQueue, appsFaultId, 1, now);
    }
}

static const char *LightName(int state)
{
    return (state == -1) ? "aucun ordre" : ((state == 1) ? "allume" : "eteint");
}

// Frein perdu, faute APPS, son R2D : ne dependent pas du couple de la chaine (B2a)
static void TestChaine_R2DAndFaults(void)
{
    // S2c : le frein agit sur le couple (S2), c'est un signal critique (T 11.9.1) : perdu, il compte comme appuye
    float fresh = TrvpPedal(true, T_NOW);
    float never = TrvpPedal(false, 0);
    float stale = TrvpPedal(true, T_NOW - 300U);
    if (fresh <= 0.0f)
    {
        vt_blocked("S2c", "T 11.9.2.d", "TRVP : pas de consigne pedale avec le frein relache, perte du frein non verifiable");
    }
    else
    {
        vt_check("S2c", "T 11.9.2.d", (never <= 0.0f) && (stale <= 0.0f),
                 "APPS 40 %% : frein relache recu -> consigne pedale %.1f %% ; frein jamais recu -> %.1f %% ; frein vieux "
                 "de 300 ms -> %.1f %% (attendu > 0, puis 0 et 0)",
                 fresh, never, stale);
    }

    // S1k : faute APPS confirmee (plus de 100 ms d'incoherence) -> TRVP la signale a Main
    int fault = TrvpAppsFault(30.0f, 70.0f, 20U);
    int noFault = TrvpAppsFault(40.0f, 40.0f, 20U);
    vt_check("S1k", "-", (fault == 1) && (noFault == 0),
             "APPS 30 %% / 70 %% pendant 200 ms -> faute envoyee a Main : %d ; APPS coherents -> %d (attendu 1, puis 0 ; "
             "-1 : rien envoye)",
             fault, noFault);

    // S1l : Main allume le voyant defaut pour une faute APPS confirmee, meme avec des donnees fraiches
    Setup();
    TestRunMain(&sMainParams, TEST_R2D_PASSES + 2U, AppsFaultInR2D);
    int beforeFault = -1;
    for (unsigned i = 0; i < TEST_R2D_PASSES; i++)
    {
        if (sTestRun.faultLight[i] != -1)
        {
            beforeFault = sTestRun.faultLight[i];
        }
    }
    int afterFault = TestLastFaultLight();
    vt_check("S1l", "-", (beforeFault == -1) && (afterFault == 1),
             "donnees fraiches -> voyant : %s ; puis faute APPS confirmee recue de TRVP -> voyant : %s (attendu aucun "
             "ordre, puis allume)",
             LightName(beforeFault), LightName(afterFault));

    // S3m : son R2D (EV 4.12.1 : de 1 a 3 s). Main en R2D pendant 2,6 s.
    Setup();
    TestRunMain(&sMainParams, 260U, TestR2DEntry);
    int on = -1;
    int off = -1;
    for (unsigned i = 0; i < sTestRun.passes; i++)
    {
        if ((on == -1) && (sTestRun.buzzer[i] == 1))
        {
            on = (int)i;
        }
        else if ((on != -1) && (off == -1) && (sTestRun.buzzer[i] == 0))
        {
            off = (int)i;
        }
    }
    long durationMs = ((on != -1) && (off != -1)) ? (long)(off - on) * (long)CONTROL_PERIOD_MS : -1L;
    vt_check("S3m", "EV 4.12.1", (on == 1) && (durationMs >= 1000L) && (durationMs <= 3000L),
             "entree en R2D a la passe 1 -> son ON a la passe %d, coupe %ld ms plus tard (attendu ON a la passe 1, puis "
             "OFF entre 1000 et 3000 ms ; -1 : jamais)",
             on, durationMs);
}

// S3p : pour entrer en R2D, le frein doit etre lu a 1 dans une trame valide. sBrakeAtSwitch : valeur du frein
// pendant le passage de l'interrupteur sur ON (-1 : signal du frein jamais recu).
static int32_t sBrakeAtSwitch;

static void SwitchOnWithBrake(unsigned pass, TickType_t now)
{
    Push(xMainQueue, tsActiveId, 1, now);
    Push(xMainQueue, sdcClosedId, 1, now);
    Push(xMainQueue, r2dSwitchId, (pass == 0U) ? 0 : 1, now);
    if (sBrakeAtSwitch >= 0)
    {
        Push(xMainQueue, brakePressedId, (pass == 0U) ? 0 : sBrakeAtSwitch, now);
    }
    Push(xMainQueue, processedThrottleId, FP_FROMFLT(40.0f), now);
    Push(xMainQueue, calculatedSpeedId, 0, now);
    Push(xMainQueue, motor_tempId, 40, now);
    Push(xMainQueue, inverter_tempId, 40, now);
    Push(xMainQueue, voltageId, 350, now);
}

// Consigne et Drive enable de la derniere passe : interrupteur sur OFF, puis sur ON avec ce frein, pedale a 40 %
static float TorqueAfterSwitchOn(int32_t brake, int *driveEnable)
{
    Setup();
    sBrakeAtSwitch = brake;
    TestRunMain(&sMainParams, TEST_R2D_PASSES, SwitchOnWithBrake);
    float torque = 0.0f;
    if (!TestLastTorque(&torque))
    {
        torque = 0.0f;
    }
    *driveEnable = sTestRun.driveEnable[sTestRun.passes - 1U];
    return torque;
}

// Un frein douteux coupe le couple (S2c), mais il ne doit jamais autoriser l'entree en R2D (EV 4.11.7)
static void TestChaine_R2DNeedsBrake(void)
{
    int enablePressed = -1;
    int enableInvalid = -1;
    int enableMissing = -1;
    float pressed = TorqueAfterSwitchOn(1, &enablePressed);
    float invalid = TorqueAfterSwitchOn(2, &enableInvalid);
    float missing = TorqueAfterSwitchOn(-1, &enableMissing);
    if ((pressed <= 0.0f) || (enablePressed != 1))
    {
        vt_blocked("S3p", "EV 4.11.7", "frein lu a 1 : pas de R2D, cas du frein douteux non verifiable");
        return;
    }
    vt_check("S3p", "EV 4.11.7",
             (invalid <= 0.0f) && (enableInvalid != 1) && (missing <= 0.0f) && (enableMissing != 1),
             "interrupteur passe sur ON, pedale 40 %% : frein lu a 1 -> consigne %.1f %%, Drive enable %d ; frein a une "
             "valeur invalide (2) -> %.1f %%, Drive enable %d ; signal du frein jamais recu -> %.1f %%, Drive enable %d "
             "(attendu du couple, puis pas de R2D dans les deux autres cas)",
             pressed, enablePressed, invalid, enableInvalid, missing, enableMissing);
}

int main(void)
{
    vt_begin("Chaine de couple TRVP -> Main -> CanTx");
    float torque = 0.0f;

    // Precondition de tout le secteur : APPS a 40 % -> une consigne > 0 sort vers l'onduleur.
    // RunChain fait la sequence R2D (T8) avant d'attendre du couple.
    bool sent = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW}, &torque);
    bool chainWorks = vt_check("B2a", "-", sent && (torque > 0.0f),
                               "APPS 40 %%, donnees fraiches -> consigne %s %.1f %% (attendu > 0)",
                               sent ? "envoyee" : "NON envoyee", torque);

    unsigned count = 0;
    TickType_t maxGap = MainSilentMaxGap(&count);
    vt_check("R1d", "-", maxGap <= 20U,
             "Main pendant 100 ms sans aucun message -> %u consigne(s), plus long ecart %lu ms (attendu au plus 20 ms : "
             "le DTI coupe apres 100 ms sans consigne)",
             count, (unsigned long)maxGap);

    TestChaine_R2DAndFaults();
    TestChaine_R2DNeedsBrake();

    if (!chainWorks)
    {
        vt_blocked("S5a", "T 11.9.4", "chaine sans couple (B2a) : coupure sur donnee perimee non verifiable");
        vt_blocked("S5c", "T 11.9.2.d", "chaine sans couple (B2a) : coupure sans temperature non verifiable");
        vt_blocked("S5d", "-", "chaine sans couple (B2a) : voyant defaut non verifiable");
        vt_blocked("S5e", "T 11.9.4", "chaine sans couple (B2a) : coupure sur temperature perimee non verifiable");
        vt_blocked("S3a", "EV 4.11.7", "chaine sans couple (B2a) : absence de R2D non verifiable");
        vt_blocked("B3f", "-", "chaine sans couple (B2a) : limiteur de vitesse non verifiable");
        return vt_end();
    }

    sent = RunChain((ChainInputs_t){40.0f, true, T_NOW - 2500U, T_NOW - 2500U}, &torque);
    vt_check("S5a", "T 11.9.4", !sent || torque <= 0.0f,
             "temperatures et tension vieilles de 2500 ms (> 500 ms) -> consigne %.1f %% (attendu 0)", torque);

    // T1 : une temperature perdue suffit a couper le couple, meme si la tension est fraiche
    sent = RunChain((ChainInputs_t){40.0f, true, T_NOW - 2500U, T_NOW}, &torque);
    vt_check("S5e", "T 11.9.4", !sent || torque <= 0.0f,
             "temperatures vieilles de 2500 ms, tension fraiche -> consigne %.1f %% (attendu 0, T1)", torque);

    sent = RunChain((ChainInputs_t){40.0f, false, 0, 0}, &torque);
    vt_check("S5c", "T 11.9.2.d", !sent || torque <= 0.0f,
             "temperatures et tension jamais recues -> consigne %.1f %% (attendu 0)", torque);
    int missingLight = FaultLightOrder();
    (void)RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW}, &torque);
    int freshLight = FaultLightOrder();
    vt_check("S5d", "-", (missingLight == 1) && (freshLight == -1),
             "temperatures et tension jamais recues -> voyant defaut %s ; donnees fraiches -> %s (attendu allume ; "
             "aucun ordre)",
             (missingLight == 1) ? "allume" : ((missingLight == 0) ? "eteint" : "sans ordre"),
             (freshLight == 1) ? "allume" : ((freshLight == 0) ? "eteint" : "aucun ordre"));

    // B3, D7 : toute la chaine des unites (roues brutes, TRVP en mm/s, Main en m/s, limiteur)
    float half = 0.0f;
    float over = 0.0f;
    bool sentHalf = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW, WheelRawFor(speedLimitMs / 2.0f)}, &half);
    bool sentOver = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW, WheelRawFor(speedLimitMs + 1.0f)}, &over);
    vt_check("B3f", "-", sentHalf && (half > 0.0f) && (!sentOver || over <= 0.0f),
             "APPS 40 %%, roues a la moitie de la limite (%.1f m/s) -> consigne %.1f %% ; a la limite + 1 m/s -> "
             "%.1f %% (attendu > 0, puis 0)",
             speedLimitMs / 2.0f, half, over);

    sent = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW, 0, true}, &torque);
    vt_check("S3a", "EV 4.11.7", !sent || torque <= 0.0f,
             "TS actif, aucune sequence R2D (frein + interrupteur) -> consigne %.1f %% (attendu 0)", torque);

    return vt_end();
}
