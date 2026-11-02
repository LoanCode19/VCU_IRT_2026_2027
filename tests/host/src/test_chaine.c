// Secteur : chaine de couple TaskRawValuesProcessing -> TaskMain -> queue CanTx
// Code teste : les deux taches en vrai, avec les queues de MainApp()
// Problemes : B2, S5, S3 de AUDIT_VCU.md, et ce que TRVP et Main font elles-memes sur plusieurs periodes : roues (B4),
// frein (S2), APPS (S1), delais (S5), rampe (R1), envoi sans attente (R3), traces (T17)
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
#include "utils.h"
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
    bool temperaturesNeverSent; // tension envoyee, temperatures jamais (S5f)
    int oldTemperature;         // 1 : moteur seul vieux de 2500 ms, 2 : onduleur seul (S5g)
    bool wheelsNeverSent;       // aucune trame de roue (R8c, T25)
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
    if (!in.wheelsNeverSent)
    {
        Push(xTRVPQueue, wheelFRId, in.wheelRaw, T_NOW);
        Push(xTRVPQueue, wheelFLId, in.wheelRaw, T_NOW);
        Push(xTRVPQueue, wheelRRId, in.wheelRaw, T_NOW);
        Push(xTRVPQueue, wheelRLId, in.wheelRaw, T_NOW);
    }
    Push(xTRVPQueue, brakePressedId, 0, T_NOW); // frein relache
    if (in.sendTemperatures && !in.temperaturesNeverSent)
    {
        Push(xMainQueue, motor_tempId, 40, (in.oldTemperature == 1) ? T_NOW - 2500U : in.temperatureTimestamp);
        Push(xMainQueue, inverter_tempId, 40, (in.oldTemperature == 2) ? T_NOW - 2500U : in.temperatureTimestamp);
    }
    if (in.sendTemperatures)
    {
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
    // La faute APPS part avec la consigne, a chaque periode, comme dans TRVP (Q8) : 0, puis la faute confirmee
    Push(xMainQueue, appsFaultId, (pass >= TEST_R2D_PASSES) ? 1 : 0, now);
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
    Push(xMainQueue, appsFaultId, 0, now); // la faute APPS part avec la consigne, comme dans TRVP (Q8)
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

// ------------------------------------------------------------------------------------------------------------------
// TRVP en vrai sur plusieurs periodes : ce que la tache elle-meme range, date et verifie (pas seulement les fonctions
// de calcul de utils.c). Ce que TRVP a envoye a Main a chaque passe ; -1 : rien.

#define TRVP_MAX_PASSES 64U

typedef void (*TrvpInputs_t)(unsigned pass, TickType_t now);
static TrvpInputs_t sTrvpInputs;
static unsigned sTrvpPass;
static float sTrvpPedal[TRVP_MAX_PASSES];
static int32_t sTrvpSpeed[TRVP_MAX_PASSES];
static int sTrvpFault[TRVP_MAX_PASSES];

static void TrvpCollect(void)
{
    dataMessage_t msg = {0};
    while (xQueueReceive(xMainQueue, &msg, 0) == pdPASS)
    {
        if (sTrvpPass >= TRVP_MAX_PASSES)
        {
            continue;
        }
        if (msg.id == processedThrottleId)
        {
            sTrvpPedal[sTrvpPass] = FP_TOFLOAT(msg.data);
        }
        else if (msg.id == calculatedSpeedId)
        {
            sTrvpSpeed[sTrvpPass] = msg.data;
        }
        else if (msg.id == appsFaultId)
        {
            sTrvpFault[sTrvpPass] = (int)msg.data;
        }
    }
}

static void TrvpWakeHook(TickType_t now)
{
    TrvpCollect();
    sTrvpPass++;
    if (sTrvpInputs != NULL)
    {
        sTrvpInputs(sTrvpPass, now);
    }
}

// TRVP pendant passes periodes, a partir de l'heure start ; inputs depose les messages de chaque passe
static void RunTrvp(TickType_t start, unsigned passes, TrvpInputs_t inputs)
{
    Setup();
    vTestSetTickCount(start);
    for (unsigned i = 0; i < TRVP_MAX_PASSES; i++)
    {
        sTrvpPedal[i] = -1.0f;
        sTrvpSpeed[i] = -1;
        sTrvpFault[i] = -1;
    }
    sTrvpPass = 0U;
    sTrvpInputs = inputs;
    if (inputs != NULL)
    {
        inputs(0U, start);
    }
    vTestSetDelayCallsBeforeEscape(passes - 1U, TrvpWakeHook);
    RunTaskOnce(TaskRawValuesProcessing, &sTrvpParams);
    vTestSetDelayCallsBeforeEscape(0U, NULL);
    TrvpCollect();
}

static void PushApps(float percent1, float percent2, TickType_t timestamp)
{
    Push(xTRVPQueue, apps1Id, (int32_t)(potmin[0] + (potmax[0] - potmin[0]) * percent1 / 100.0f), timestamp);
    Push(xTRVPQueue, apps2Id, (int32_t)(potmin[1] + (potmax[1] - potmin[1]) * percent2 / 100.0f), timestamp);
}

// Vitesses des quatre roues (m/s) et heure de leurs trames ; < 0 : roue non envoyee
static float sWheelMs[4];
static TickType_t sWheelTimestamp[2]; // roues avant, roues arriere

static void WheelsOnce(unsigned pass, TickType_t now)
{
    (void)now;
    if (pass == 0U)
    {
        for (int i = 0; i < 4; i++)
        {
            if (sWheelMs[i] >= 0.0f)
            {
                Push(xTRVPQueue, (MessageIds)(wheelFRId + i), WheelRawFor(sWheelMs[i]), sWheelTimestamp[i / 2]);
            }
        }
    }
}

// Vitesse envoyee par TRVP apres une passe, et ce que donnent les memes roues converties par le code (mm/s)
static int32_t TrvpSpeed(float fr, float fl, float rr, float rl, TickType_t start, TickType_t frontTimestamp)
{
    sWheelMs[0] = fr;
    sWheelMs[1] = fl;
    sWheelMs[2] = rr;
    sWheelMs[3] = rl;
    sWheelTimestamp[0] = frontTimestamp;
    sWheelTimestamp[1] = start;
    RunTrvp(start, 1U, WheelsOnce);
    return sTrvpSpeed[0];
}

static int32_t MmS(float speedMs)
{
    return WheelRawToMmS(WheelRawFor(speedMs));
}

// Frein a chaque passe (valeur brake, -1 : jamais envoye) ; APPS a 40 % a la premiere passe, ou a chaque passe
static int32_t sBrakeValue;
static bool sAppsEveryPass;

static void BrakeAndApps(unsigned pass, TickType_t now)
{
    if ((pass == 0U) || sAppsEveryPass)
    {
        PushApps(40.0f, 40.0f, now);
    }
    if (sBrakeValue >= 0)
    {
        Push(xTRVPQueue, brakePressedId, sBrakeValue, now);
    }
}

static float TrvpPedalAt(TickType_t start, unsigned passes, int32_t brake, bool appsEveryPass, unsigned pass)
{
    sBrakeValue = brake;
    sAppsEveryPass = appsEveryPass;
    RunTrvp(start, passes, BrakeAndApps);
    return sTrvpPedal[pass];
}

// S1o : APPS incoherents pendant 150 ms, coherents a 40 %, pedale relachee, puis de nouveau a 40 %
static void AppsFaultThenRelease(unsigned pass, TickType_t now)
{
    if (pass < 15U)
    {
        PushApps(30.0f, 70.0f, now);
    }
    else if ((pass < 25U) || (pass >= 28U))
    {
        PushApps(40.0f, 40.0f, now);
    }
    else
    {
        PushApps(2.0f, 2.0f, now);
    }
    Push(xTRVPQueue, brakePressedId, 0, now);
}

// S1m : seul le frein arrive, les APPS jamais
static void BrakeOnly(unsigned pass, TickType_t now)
{
    (void)pass;
    Push(xTRVPQueue, brakePressedId, 0, now);
}

// R8 (T25) : APPS a 40 % et frein relache a chaque passe, sans roue
static void AppsNoWheels(unsigned pass, TickType_t now)
{
    (void)pass;
    PushApps(40.0f, 40.0f, now);
    Push(xTRVPQueue, brakePressedId, 0, now);
}

// R8b : en plus, les quatre roues a 10 m/s, a la premiere passe seulement
static void WheelsOnceThenSilent(unsigned pass, TickType_t now)
{
    AppsNoWheels(pass, now);
    if (pass == 0U)
    {
        for (int i = 0; i < 4; i++)
        {
            Push(xTRVPQueue, (MessageIds)(wheelFRId + i), WheelRawFor(10.0f), now);
        }
    }
}

// R8d, R8e : les deux roues avant seulement, a chaque passe. D'accord a 10 m/s avant la passe sAgreeUntil, puis a 10
// et 15 m/s : plus de WHEEL_MAX_DIFF_MMS d'ecart.
static unsigned sAgreeUntil;

static void FrontWheelsDisagree(unsigned pass, TickType_t now)
{
    AppsNoWheels(pass, now);
    Push(xTRVPQueue, wheelFRId, WheelRawFor(10.0f), now);
    Push(xTRVPQueue, wheelFLId, WheelRawFor((pass < sAgreeUntil) ? 10.0f : 15.0f), now);
}

static void TestChaine_Trvp(void)
{
    // B4 : chaque roue est rangee a sa place. Quatre vitesses differentes, une aberrante -> moyenne des deux du milieu.
    int32_t speed = TrvpSpeed(8.0f, 10.0f, 10.2f, 30.0f, T_NOW, T_NOW);
    int32_t expected = (MmS(10.0f) + MmS(10.2f)) / 2;
    vt_check("B4c", "-", speed == expected,
             "TRVP, roues FR 8, FL 10, RR 10,2, RL 30 m/s -> vitesse %ld mm/s (attendu %ld : moyenne des deux du milieu)",
             (long)speed, (long)expected);

    // B4 : au demarrage, une roue jamais recue ne compte pas (elle vaudrait 0)
    speed = TrvpSpeed(10.0f, 10.0f, -1.0f, -1.0f, 100U, 100U);
    vt_check("B4d", "-", speed == MmS(10.0f),
             "TRVP a t = 100 ms, roues avant a 10 m/s, roues arriere jamais recues -> vitesse %ld mm/s (attendu %ld)",
             (long)speed, (long)MmS(10.0f));

    // B4 : une roue perimee (plus de WHEEL_TIMEOUT_MS) ne compte plus
    speed = TrvpSpeed(20.0f, 20.0f, 10.0f, 10.0f, T_NOW, T_NOW - (WHEEL_TIMEOUT_MS + 100U));
    vt_check("B4e", "-", speed == MmS(10.0f),
             "TRVP, roues avant a 20 m/s vieilles de %d ms, roues arriere a 10 m/s -> vitesse %ld mm/s (attendu %ld : "
             "roues avant ignorees)",
             WHEEL_TIMEOUT_MS + 100, (long)speed, (long)MmS(10.0f));

    // S2 : au demarrage, un frein jamais recu compte comme appuye, meme avant son delai de 100 ms
    float released = TrvpPedalAt(50U, 1U, 0, false, 0U);
    float never = TrvpPedalAt(50U, 1U, -1, false, 0U);
    float invalid = TrvpPedalAt(T_NOW, 1U, 2, false, 0U);
    if (released <= 0.0f)
    {
        vt_blocked("S2d", "T 11.9.2.d", "TRVP : pas de consigne pedale avec le frein relache, frein absent non verifiable");
        vt_blocked("S2e", "T 11.9.2.c", "TRVP : pas de consigne pedale avec le frein relache, frein invalide non verifiable");
    }
    else
    {
        vt_check("S2d", "T 11.9.2.d", never == 0.0f,
                 "TRVP a t = 50 ms, APPS 40 %% : frein relache -> consigne %.1f %% ; frein jamais recu -> %.1f %% "
                 "(attendu > 0, puis 0)",
                 released, never);
        // T19 bis : une valeur autre que 0 ou 1 rend le frein douteux, donc appuye
        vt_check("S2e", "T 11.9.2.c", invalid == 0.0f,
                 "TRVP, APPS 40 %%, frein a 2 (valeur invalide) -> consigne %.1f %% (attendu 0)", invalid);
    }

    // S1 : APPS recus une seule fois -> perimes apres APPS_TIMEOUT_MS, la consigne tombe a 0
    float first = TrvpPedalAt(T_NOW, 26U, 0, false, 0U);
    float later = sTrvpPedal[25];
    vt_check("S1n", "T 11.9.2.d", (first > 0.0f) && (later == 0.0f),
             "TRVP, APPS a 40 %% recus une seule fois -> consigne %.1f %% puis, 250 ms plus tard, %.1f %% (attendu > 0, "
             "puis 0 : APPS perimes apres %d ms)",
             first, later, APPS_TIMEOUT_MS);

    // S1, D2 : la faute confirmee coupe la consigne dans la tache elle-meme, jusqu'a la pedale relachee
    RunTrvp(T_NOW, 31U, AppsFaultThenRelease);
    vt_check("S1o", "T 11.8.8", (sTrvpPedal[24] == 0.0f) && (sTrvpPedal[30] > 0.0f),
             "TRVP, APPS 30 %% / 70 %% pendant 150 ms, puis coherents a 40 %% -> consigne %.1f %% ; pedale relachee "
             "puis 40 %% -> %.1f %% (attendu 0 : faute confirmee, puis > 0 : D2)",
             sTrvpPedal[24], sTrvpPedal[30]);

    // S1 : APPS jamais recus depuis le demarrage -> incoherence, confirmee apres 100 ms (voyant defaut)
    RunTrvp(0U, 16U, BrakeOnly);
    vt_check("S1m", "T 11.8.9", sTrvpFault[15] == 1,
             "TRVP depuis t = 0, APPS jamais recus -> faute APPS envoyee a Main a 150 ms : %d (attendu 1 ; -1 : rien "
             "envoye)",
             sTrvpFault[15]);

    // R8 (T25, decide le 01/11/2026, revisable) : sans vitesse sure, TRVP n'envoie plus de vitesse. Main la voit perimee
    // apres SPEED_TIMEOUT_MS : couple 0 et voyant (T2). La consigne, elle, part toujours.
    RunTrvp(T_NOW, 5U, AppsNoWheels);
    bool noSpeed = true;
    for (unsigned i = 0; i < 5U; i++)
    {
        noSpeed = noSpeed && (sTrvpSpeed[i] == -1);
    }
    vt_check("R8a", "-", noSpeed && (sTrvpPedal[4] > 0.0f),
             "TRVP, APPS a 40 %%, roues jamais recues -> vitesse envoyee : %ld ; consigne %.1f %% (attendu -1 : aucune "
             "vitesse, et une consigne > 0)",
             (long)sTrvpSpeed[4], sTrvpPedal[4]);

    unsigned wheelPasses = (unsigned)(WHEEL_TIMEOUT_MS / (int)CONTROL_PERIOD_MS);
    RunTrvp(T_NOW, wheelPasses + 5U, WheelsOnceThenSilent);
    vt_check("R8b", "-", (sTrvpSpeed[wheelPasses] == MmS(10.0f)) && (sTrvpSpeed[wheelPasses + 4U] == -1),
             "TRVP, roues a 10 m/s recues une seule fois -> vitesse %ld mm/s a %d ms, puis %ld a %d ms (attendu %ld, puis "
             "-1 : plus de vitesse quand la derniere roue est perimee)",
             (long)sTrvpSpeed[wheelPasses], WHEEL_TIMEOUT_MS, (long)sTrvpSpeed[wheelPasses + 4U],
             WHEEL_TIMEOUT_MS + 40, (long)MmS(10.0f));

    // Roues en desaccord (T25) : la derniere vitesse sure pendant au plus SPEED_HOLD_MS, puis plus de vitesse
    unsigned holdPasses = (unsigned)(SPEED_HOLD_MS / (int)CONTROL_PERIOD_MS);
    sAgreeUntil = 5U;
    RunTrvp(T_NOW, sAgreeUntil + holdPasses + 5U, FrontWheelsDisagree);
    int32_t agreed = sTrvpSpeed[sAgreeUntil - 1U];
    int32_t held = sTrvpSpeed[sAgreeUntil + holdPasses];
    int32_t after = sTrvpSpeed[sAgreeUntil + holdPasses + 4U];
    vt_check("R8d", "-", (agreed == MmS(10.0f)) && (held == MmS(10.0f)) && (after == -1),
             "TRVP, roues avant a 10 m/s, puis a 10 et 15 m/s -> vitesse %ld mm/s ; %d ms de desaccord -> %ld ; %d ms "
             "-> %ld (attendu %ld, %ld, puis -1 : la derniere vitesse sure, %d ms au plus)",
             (long)agreed, SPEED_HOLD_MS, (long)held, SPEED_HOLD_MS + 40, (long)after, (long)MmS(10.0f),
             (long)MmS(10.0f), SPEED_HOLD_MS);

    sAgreeUntil = 0U;
    RunTrvp(T_NOW, 10U, FrontWheelsDisagree);
    bool neverSpeed = true;
    for (unsigned i = 0; i < 10U; i++)
    {
        neverSpeed = neverSpeed && (sTrvpSpeed[i] == -1);
    }
    vt_check("R8e", "-", neverSpeed,
             "TRVP, roues avant a 10 et 15 m/s des le demarrage -> vitesse envoyee : %ld (attendu -1 : pas de vitesse "
             "sure a garder)",
             (long)sTrvpSpeed[9]);

    // R8c : toute la chaine en R2D. Roues a 0 m/s : du couple ; roues jamais recues : couple 0 et voyant.
    float withWheels = 0.0f;
    float withoutWheels = 0.0f;
    bool sentWith = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW}, &withWheels);
    bool sentWithout = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW, 0, false, false, 0, true}, &withoutWheels);
    int lightWithout = FaultLightOrder();
    if (!sentWith || (withWheels <= 0.0f))
    {
        vt_blocked("R8c", "-", "chaine sans couple avec les roues : perte des roues non verifiable");
    }
    else
    {
        vt_check("R8c", "-", (!sentWithout || (withoutWheels <= 0.0f)) && (lightWithout == 1),
                 "chaine en R2D, APPS 40 %% : roues a 0 m/s -> consigne %.1f %% ; roues jamais recues -> %.1f %%, "
                 "voyant %s (attendu > 0, puis 0 et allume)",
                 withWheels, sentWithout ? withoutWheels : 0.0f, LightName(lightWithout));
    }
}

// ------------------------------------------------------------------------------------------------------------------
// TaskMain en vrai sur plusieurs periodes : donnees fraiches a chaque passe, sequence R2D, et une donnee qui cesse
// d'arriver a partir de la passe sStopPass (sStopId ; tsActiveId : toute la trame d'etat ; -1 : aucune).

static int sStopId;
static unsigned sStopPass;
static bool sNeverSent; // sStopId n'est jamais envoye (meme a la passe 0)

static void MainData(unsigned pass, TickType_t now)
{
    bool stopped = sNeverSent || (pass >= sStopPass);
    if (!(stopped && (sStopId == (int)tsActiveId)))
    {
        TestR2DEntry(pass, now);
    }
    const struct
    {
        MessageIds id;
        int32_t value;
    } data[] = {{processedThrottleId, FP_FROMFLT(40.0f)}, {appsFaultId, 0}, {calculatedSpeedId, 0}, {motor_tempId, 40},
                {inverter_tempId, 40}, {voltageId, 350}}; // la faute APPS part avec la consigne, comme dans TRVP (Q8)
    for (unsigned i = 0; i < sizeof(data) / sizeof(data[0]); i++)
    {
        if (!(stopped && (sStopId == (int)data[i].id)))
        {
            Push(xMainQueue, data[i].id, data[i].value, now);
        }
    }
}

// Main pendant passes periodes a partir de start ; stopId cesse d'arriver a la passe stopPass
static void RunMainData(TickType_t start, unsigned passes, int stopId, unsigned stopPass, bool neverSent)
{
    Setup();
    vTestSetTickCount(start);
    sStopId = stopId;
    sStopPass = stopPass;
    sNeverSent = neverSent;
    TestRunMain(&sMainParams, passes, MainData);
}

static float TorqueAt(unsigned pass)
{
    return sTestRun.torqueSent[pass] ? sTestRun.torque[pass] : 0.0f;
}

// Une donnee qui cesse d'arriver : couple, puis 0 et voyant defaut apres son delai
static void CheckStops(const char *id, const char *rule, int stopId, int timeoutMs, const char *name)
{
    unsigned passes = (unsigned)(timeoutMs / (int)CONTROL_PERIOD_MS) + 10U;
    RunMainData(T_NOW, passes, stopId, 5U, false);
    float before = TorqueAt(4U);
    float after = TorqueAt(passes - 1U);
    int light = TestLastFaultLight();
    if (before <= 0.0f)
    {
        vt_blocked(id, rule, "Main sans couple avec des donnees fraiches : perte de la %s non verifiable", name);
        return;
    }
    vt_check(id, rule, (after == 0.0f) && (light == 1),
             "en R2D, %s plus envoyee apres 50 ms -> consigne %.1f %%, puis %.1f %% %d ms plus tard, voyant %s (attendu "
             "> 0, puis 0 et voyant allume)",
             name, before, after, (int)(passes - 5U) * (int)CONTROL_PERIOD_MS, (light == 1) ? "allume" : "eteint");
}

// R1e : TS actif sans R2D pendant 200 ms, pedale a 40 % ; puis entree en R2D a la passe 20
static void TsThenR2D(unsigned pass, TickType_t now)
{
    TestVehicle_t vehicle = {true, true, false, false};
    if (pass == 20U)
    {
        vehicle = (TestVehicle_t){true, true, true, true};
    }
    else if (pass > 20U)
    {
        vehicle = (TestVehicle_t){true, true, false, true};
    }
    TestPushVehicle(vehicle, now);
    Push(xMainQueue, processedThrottleId, FP_FROMFLT(40.0f), now);
    Push(xMainQueue, appsFaultId, 0, now); // la faute APPS part avec la consigne, comme dans TRVP (Q8)
    Push(xMainQueue, calculatedSpeedId, 0, now);
    Push(xMainQueue, motor_tempId, 40, now);
    Push(xMainQueue, inverter_tempId, 40, now);
    Push(xMainQueue, voltageId, 350, now);
}

// R1f : en R2D, Main se reveille 15 ms en retard a la passe 10
static void LateWake(unsigned pass, TickType_t now)
{
    if (pass == 10U)
    {
        now += 15U;
        vTestSetTickCount(now);
    }
    MainData(pass, now);
}

// S5 : l'etat d'une donnee selon son age (updateDataTimeStatus, utils.c)
static void TestChaine_DataAge(void)
{
    dataMessage_t never = {.timestamp = 0, .timeStatus = STATUS_NOT_READY};
    dataMessage_t exact = {.timestamp = T_NOW - 100U, .timeStatus = STATUS_TIME_OK};
    dataMessage_t old = {.timestamp = T_NOW - 101U, .timeStatus = STATUS_TIME_OK};
    updateDataTimeStatus(&never, 1, 100, T_NOW);
    updateDataTimeStatus(&exact, 1, 100, T_NOW);
    updateDataTimeStatus(&old, 1, 100, T_NOW);
    vt_check("S5k", "T 11.9.2.d",
             (never.timeStatus == STATUS_NOT_READY) && (exact.timeStatus == STATUS_TIME_OK) &&
                 (old.timeStatus == STATUS_TIMEOUT),
             "delai de 100 ms, a t = %u ms : donnee jamais recue -> etat %d ; vieille de 100 ms -> %d ; de 101 ms -> %d "
             "(attendu %d jamais recue, %d fraiche, %d perimee)",
             (unsigned)T_NOW, never.timeStatus, exact.timeStatus, old.timeStatus, STATUS_NOT_READY, STATUS_TIME_OK,
             STATUS_TIMEOUT);
}

// R3 : une queue pleine ne bloque jamais la tache qui depose ; la perte est comptee (SendDataMessage, utils.c)
static void TestChaine_SendNeverBlocks(void)
{
    Setup();
    dataMessage_t filler = {.id = calculatedSpeedId};
    while (xQueueSend(xCanTxQueue, &filler, 0) == pdPASS)
    {
    }
    uint32_t lost = 0U;
    SendDataMessage(xCanTxQueue, inverterTorqueCmdId, FP_FROMFLT(30.0f), T_NOW, &lost);
    SendDataMessage(xCanTxQueue, driveEnableCmdId, 1, T_NOW, &lost);
    vt_check("R3a", "-", lost == 2U,
             "queue de CanTx pleine, 2 messages deposes -> %lu perte(s) comptee(s), sans attendre (attendu 2)",
             (unsigned long)lost);
}

// T17 : TRVP et Main ecrivent leur etat une fois par seconde, jamais a chaque passage
extern unsigned uxTestConsolePrintCount;

static void TestChaine_Traces(void)
{
    sBrakeValue = 0;
    sAppsEveryPass = true;
    unsigned before = uxTestConsolePrintCount;
    RunTrvp(T_NOW, 201U, BrakeAndApps);
    unsigned trvpPrints = uxTestConsolePrintCount - before;
    before = uxTestConsolePrintCount;
    RunMainData(T_NOW, 201U, -1, 0U, false);
    unsigned mainPrints = uxTestConsolePrintCount - before;
    vt_check("T17a", "-", (trvpPrints <= 30U) && (mainPrints <= 30U),
             "2 s de marche (201 passes) -> %u ligne(s) de trace pour TRVP, %u pour Main (attendu au plus 30 chacune : "
             "l'etat une fois par seconde, une dizaine de lignes)",
             trvpPrints, mainPrints);
}

static void TestChaine_Main(void)
{
    // S5, T2 : chaque donnee seule, quand elle cesse d'arriver
    CheckStops("S5h", "T 11.9.4", voltageId, VOLTAGE_TIMEOUT_MS, "tension");
    CheckStops("S5i", "T 11.9.4", calculatedSpeedId, SPEED_TIMEOUT_MS, "vitesse");

    // Q8 (T28, decide le 01/11/2026, revisable) : la faute APPS a un delai dans Main. Plus recue ou jamais recue, elle
    // compte comme une faute et allume le voyant, comme un frein douteux (T19 bis).
    unsigned q8Passes = (unsigned)(THROTTLE_CMD_TIMEOUT_MS / (int)CONTROL_PERIOD_MS) + 10U;
    RunMainData(T_NOW, q8Passes, (int)appsFaultId, 5U, false);
    int lightBefore = -1;
    for (unsigned i = 0; i < 5U; i++)
    {
        lightBefore = (sTestRun.faultLight[i] != -1) ? sTestRun.faultLight[i] : lightBefore;
    }
    int lightStopped = TestLastFaultLight();
    RunMainData(T_NOW, q8Passes, (int)appsFaultId, 0U, true);
    int lightNever = TestLastFaultLight();
    vt_check("Q8a", "-", (lightBefore == -1) && (lightStopped == 1) && (lightNever == 1),
             "consigne fraiche : faute APPS a 0 -> voyant %s ; plus recue apres 50 ms -> %s ; jamais recue -> %s "
             "(attendu aucun ordre, puis allume et allume)",
             LightName(lightBefore), LightName(lightStopped), LightName(lightNever));

    // T19 : trame d'etat perdue en R2D -> sortie du R2D, Drive enable 0 et voyant, apres VEHICLE_STATE_TIMEOUT_MS
    RunMainData(T_NOW, 20U, (int)tsActiveId, 5U, false);
    float inR2D = TorqueAt(4U);
    float lost = TorqueAt(19U);
    int enable = sTestRun.driveEnable[19];
    int light = TestLastFaultLight();
    if (inR2D <= 0.0f)
    {
        vt_blocked("S3q", "T 11.9.2.d", "Main sans couple en R2D : perte de la trame d'etat non verifiable");
    }
    else
    {
        vt_check("S3q", "T 11.9.2.d", (lost == 0.0f) && (enable == 0) && (light == 1),
                 "en R2D, trame d'etat plus recue apres 50 ms -> consigne %.1f %%, puis %.1f %% 140 ms plus tard, Drive "
                 "enable %d, voyant %s (attendu > 0, puis 0, 0 et allume)",
                 inR2D, lost, enable, (light == 1) ? "allume" : "eteint");
    }

    // S5 : au demarrage, chaque donnee jamais recue coupe le couple, meme avant son delai
    RunMainData(20U, TEST_R2D_PASSES + 2U, -1, 0U, false);
    float allSent = TorqueAt(TEST_R2D_PASSES + 1U);
    const struct
    {
        MessageIds id;
        const char *name;
    } inputs[] = {{processedThrottleId, "consigne"}, {calculatedSpeedId, "vitesse"}, {motor_tempId, "temp. moteur"},
                  {inverter_tempId, "temp. onduleur"}, {voltageId, "tension"}};
    bool allCut = true;
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++)
    {
        RunMainData(20U, TEST_R2D_PASSES + 2U, (int)inputs[i].id, 0U, true);
        float torque = TorqueAt(TEST_R2D_PASSES + 1U);
        if (torque != 0.0f)
        {
            allCut = false;
            vt_note("%s jamais recue : consigne %.1f %%", inputs[i].name, torque);
        }
    }
    if (allSent <= 0.0f)
    {
        vt_blocked("S5j", "T 11.9.2.d", "Main sans couple au demarrage : donnees jamais recues non verifiables");
    }
    else
    {
        vt_check("S5j", "T 11.9.2.d", allCut,
                 "au demarrage (t = 20 ms), toutes les donnees -> consigne %.1f %% ; une seule jamais recue (consigne, "
                 "vitesse, temperature moteur, onduleur, tension) -> %s (attendu > 0, puis 0 a chaque fois)",
                 allSent, allCut ? "0 a chaque fois" : "du couple");
    }

    // R1 : la rampe repart de 0 a l'entree en R2D, meme si la pedale etait enfoncee avant
    Setup();
    TestRunMain(&sMainParams, 22U, TsThenR2D);
    float step = throtRampPerS * (float)CONTROL_PERIOD_MS / 1000.0f;
    float entry = TorqueAt(20U);
    vt_check("R1e", "-", (entry > 0.0f) && (entry <= step + 0.05f),
             "TS actif 200 ms sans R2D, pedale a 40 %%, puis entree en R2D -> premiere consigne %.1f %% (attendu entre 0 "
             "et %.1f : la rampe repart de 0)",
             entry, step);

    // R1 : la rampe avance du temps reellement ecoule (passe en retard de 15 ms : 25 ms, bornes a 20 ms)
    Setup();
    sStopId = -1;
    sStopPass = 0U;
    sNeverSent = false;
    TestRunMain(&sMainParams, 12U, LateWake);
    float lateStep = TorqueAt(10U) - TorqueAt(9U);
    float maxStep = throtRampPerS * (float)CONTROL_MAX_ELAPSED_MS / 1000.0f;
    vt_check("R1f", "-", (lateStep > step + 0.05f) && (lateStep <= maxStep + 0.05f),
             "en R2D, Main reveillee 15 ms en retard -> la consigne monte de %.1f %% (attendu plus de %.1f, au plus "
             "%.1f : rampe sur le temps ecoule, bornee a %u ms)",
             lateStep, step, maxStep, (unsigned)CONTROL_MAX_ELAPSED_MS);
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
    TestChaine_Trvp();
    TestChaine_Main();
    TestChaine_DataAge();
    TestChaine_Traces();
    TestChaine_SendNeverBlocks();

    if (!chainWorks)
    {
        vt_blocked("S5a", "T 11.9.4", "chaine sans couple (B2a) : coupure sur donnee perimee non verifiable");
        vt_blocked("S5c", "T 11.9.2.d", "chaine sans couple (B2a) : coupure sans temperature non verifiable");
        vt_blocked("S5d", "-", "chaine sans couple (B2a) : voyant defaut non verifiable");
        vt_blocked("S5e", "T 11.9.4", "chaine sans couple (B2a) : coupure sur temperature perimee non verifiable");
        vt_blocked("S5f", "T 11.9.2.d", "chaine sans couple (B2a) : coupure sans temperature non verifiable");
        vt_blocked("S5g", "T 11.9.4", "chaine sans couple (B2a) : coupure sur une temperature perimee non verifiable");
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

    // T1 : des temperatures jamais recues coupent le couple, meme avec une tension fraiche. S5c ne le montre pas a
    // lui seul : la tension jamais recue vaut 0 V, et la reduction sur tension basse (T11) coupe deja le couple.
    sent = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW, 0, false, true}, &torque);
    vt_check("S5f", "T 11.9.2.d", !sent || torque <= 0.0f,
             "temperatures jamais recues, tension fraiche -> consigne %.1f %% (attendu 0, T1)", torque);

    // T1 : chaque temperature compte seule. Une seule perimee, l'autre et la tension fraiches -> couple coupe.
    float motorOld = 0.0f;
    float inverterOld = 0.0f;
    bool sentMotorOld = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW, 0, false, false, 1}, &motorOld);
    bool sentInverterOld = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW, 0, false, false, 2}, &inverterOld);
    vt_check("S5g", "T 11.9.4", (!sentMotorOld || motorOld <= 0.0f) && (!sentInverterOld || inverterOld <= 0.0f),
             "temperature moteur seule vieille de 2500 ms -> consigne %.1f %% ; onduleur seul -> %.1f %% (attendu 0, puis "
             "0, T1)",
             motorOld, inverterOld);
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
