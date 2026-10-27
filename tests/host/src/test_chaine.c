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

// Une passe de TRVP puis une passe de Main. Renvoie false si aucune consigne n'est sortie.
static bool RunChain(ChainInputs_t in, float *torque)
{
    Setup();
    int32_t raw0 = (int32_t)(potmin[0] + (potmax[0] - potmin[0]) * in.appsPercent / 100.0f);
    int32_t raw1 = (int32_t)(potmin[1] + (potmax[1] - potmin[1]) * in.appsPercent / 100.0f);
    Push(xTRVPQueue, apps1Id, raw0, T_NOW);
    Push(xTRVPQueue, apps2Id, raw1, T_NOW);
    Push(xTRVPQueue, wheelFRId, 0, T_NOW);
    Push(xTRVPQueue, wheelFLId, 0, T_NOW);
    Push(xTRVPQueue, wheelRRId, 0, T_NOW);
    Push(xTRVPQueue, wheelRLId, 0, T_NOW);
    if (in.sendTemperatures)
    {
        Push(xMainQueue, motor_tempId, 40, in.temperatureTimestamp);
        Push(xMainQueue, inverter_tempId, 40, in.temperatureTimestamp);
        Push(xMainQueue, voltageId, 350, in.voltageTimestamp);
    }

    RunTaskOnce(TaskRawValuesProcessing, &sTrvpParams);
    RunTaskOnce(TaskMain, &sMainParams);

    dataMessage_t cmd = {0};
    if (xQueueReceive(xCanTxQueue, &cmd, 0) != pdPASS)
    {
        return false;
    }
    *torque = FP_TOFLOAT(cmd.data);
    return true;
}

// Ordre envoye par Main a l'IHM pour le voyant defaut, apres RunChain : -1 aucun, 0 eteint, 1 allume
static int FaultLightOrder(void)
{
    IHMOrder_t order;
    int state = -1;
    while (xQueueReceive(xIHMQueue, &order, 0) == pdPASS)
    {
        if (order.order == FaultLight)
        {
            state = order.state ? 1 : 0;
        }
    }
    return state;
}

int main(void)
{
    vt_begin("Chaine de couple TRVP -> Main -> CanTx");
    float torque = 0.0f;

    // Precondition de tout le secteur : APPS a 40 % -> une consigne > 0 sort vers l'onduleur.
    // Quand le mode R2D existera (S3), ce test devra d'abord faire la sequence R2D.
    bool sent = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW}, &torque);
    bool chainWorks = vt_check("B2a", "-", sent && (torque > 0.0f),
                               "APPS 40 %%, donnees fraiches -> consigne %s %.1f %% (attendu > 0)",
                               sent ? "envoyee" : "NON envoyee", torque);

    if (!chainWorks)
    {
        vt_blocked("S5a", "T 11.9.4", "chaine sans couple (B2a) : coupure sur donnee perimee non verifiable");
        vt_blocked("S5c", "T 11.9.2.d", "chaine sans couple (B2a) : coupure sans temperature non verifiable");
        vt_blocked("S5d", "-", "chaine sans couple (B2a) : voyant defaut non verifiable");
        vt_blocked("S5e", "T 11.9.4", "chaine sans couple (B2a) : coupure sur temperature perimee non verifiable");
        vt_blocked("S3a", "EV 4.11.7", "chaine sans couple (B2a) : absence de R2D non verifiable");
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

    sent = RunChain((ChainInputs_t){40.0f, true, T_NOW, T_NOW}, &torque);
    vt_check("S3a", "EV 4.11.7", !sent || torque <= 0.0f,
             "aucune sequence R2D (frein + bouton) -> consigne %.1f %% (attendu 0)", torque);

    return vt_end();
}
