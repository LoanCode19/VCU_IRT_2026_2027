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
