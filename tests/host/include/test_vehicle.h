#ifndef TEST_HOST_TEST_VEHICLE_H
#define TEST_HOST_TEST_VEHICLE_H

// Outils des tests qui font tourner TaskMain plusieurs periodes : etat du vehicule a chaque passe (trame
// provisoire 0x012, T19), sequence Ready-to-Drive (S3, T8), et ce que Main a envoye pendant chaque passe.
// A inclure apres test_app.h.

#include <stdbool.h>
#include <string.h>

#include "structs.h"
#include "queueHandles.h"
#include "my_fp.h"
#include "TaskMain.h"
#include "test_app.h"

#define TEST_MAX_PASSES 400U
#define TEST_R2D_PASSES 3U // passes de TestR2DEntry : interrupteur OFF, puis ON avec le frein, puis frein relache

typedef struct
{
    bool tsActive;
    bool sdcClosed;
    bool brake;
    bool r2dSwitch;
} TestVehicle_t;

// Appelee avant chaque passe de Main (pass = 0 pour la premiere) : depose les messages de cette passe
typedef void (*TestPassInputs_t)(unsigned pass, TickType_t now);

// Ce que Main a envoye pendant chaque passe. -1 : rien envoye.
typedef struct
{
    unsigned passes;
    bool torqueSent[TEST_MAX_PASSES];
    float torque[TEST_MAX_PASSES];
    TickType_t torqueTick[TEST_MAX_PASSES];
    int driveEnable[TEST_MAX_PASSES];
    int buzzer[TEST_MAX_PASSES];
    int faultLight[TEST_MAX_PASSES];
} TestMainRun_t;

static TestMainRun_t sTestRun;
static TestPassInputs_t sTestInputs;
static unsigned sTestPass;

// Etat du vehicule vers Main, comme TaskCanRx le route depuis la trame 0x012
static void TestPushVehicle(TestVehicle_t vehicle, TickType_t now)
{
    const MessageIds ids[4] = {tsActiveId, sdcClosedId, brakePressedId, r2dSwitchId};
    const bool values[4] = {vehicle.tsActive, vehicle.sdcClosed, vehicle.brake, vehicle.r2dSwitch};
    for (unsigned i = 0; i < 4U; i++)
    {
        dataMessage_t msg = {.id = ids[i], .data = values[i] ? 1 : 0, .timestamp = now, .timeStatus = STATUS_TIME_OK};
        (void)xQueueSend(xMainQueue, &msg, 0);
    }
}

// Sequence Ready-to-Drive (T8) : TS actif et interrupteur sur OFF, puis interrupteur sur ON pendant un freinage,
// puis frein relache. Main est en R2D a partir de la passe 1.
static TestVehicle_t TestR2DState(unsigned pass)
{
    if (pass == 0U)
    {
        return (TestVehicle_t){true, true, false, false};
    }
    if (pass == 1U)
    {
        return (TestVehicle_t){true, true, true, true};
    }
    return (TestVehicle_t){true, true, false, true};
}

static void TestR2DEntry(unsigned pass, TickType_t now)
{
    TestPushVehicle(TestR2DState(pass), now);
}

// TS actif et SDC ferme, interrupteur jamais actionne : pas de R2D
static void TestTsOnly(unsigned pass, TickType_t now)
{
    (void)pass;
    TestPushVehicle((TestVehicle_t){true, true, false, false}, now);
}

// Vide les queues de CanTx et de l'IHM : ce que Main a envoye pendant la passe
static void TestCollect(unsigned pass)
{
    if (pass >= TEST_MAX_PASSES)
    {
        return;
    }
    dataMessage_t msg;
    while (xQueueReceive(xCanTxQueue, &msg, 0) == pdPASS)
    {
        if (msg.id == inverterTorqueCmdId)
        {
            sTestRun.torqueSent[pass] = true;
            sTestRun.torque[pass] = FP_TOFLOAT(msg.data);
            sTestRun.torqueTick[pass] = msg.timestamp;
        }
        else if (msg.id == driveEnableCmdId)
        {
            sTestRun.driveEnable[pass] = (msg.data != 0) ? 1 : 0;
        }
    }
    IHMOrder_t order;
    while (xQueueReceive(xIHMQueue, &order, 0) == pdPASS)
    {
        if (order.order == R2Dbuzzer)
        {
            sTestRun.buzzer[pass] = order.state ? 1 : 0;
        }
        else if (order.order == FaultLight)
        {
            sTestRun.faultLight[pass] = order.state ? 1 : 0;
        }
    }
}

static void TestMainWakeHook(TickType_t now)
{
    TestCollect(sTestPass);
    sTestPass++;
    if (sTestInputs != NULL)
    {
        sTestInputs(sTestPass, now);
    }
}

// Fait tourner TaskMain pendant passes periodes. inputs depose les messages de chaque passe (NULL : aucun).
static void TestRunMain(MainParams_t *params, unsigned passes, TestPassInputs_t inputs)
{
    memset(&sTestRun, 0, sizeof(sTestRun));
    for (unsigned i = 0; i < TEST_MAX_PASSES; i++)
    {
        sTestRun.driveEnable[i] = -1;
        sTestRun.buzzer[i] = -1;
        sTestRun.faultLight[i] = -1;
    }
    sTestRun.passes = passes;
    sTestPass = 0U;
    sTestInputs = inputs;
    if (inputs != NULL)
    {
        inputs(0U, xTaskGetTickCount());
    }
    vTestSetDelayCallsBeforeEscape(passes - 1U, TestMainWakeHook);
    RunTaskOnce(TaskMain, params);
    vTestSetDelayCallsBeforeEscape(0U, NULL);
    TestCollect(sTestPass);
}

// Consigne envoyee pendant la derniere passe. false si Main n'en a pas envoye.
static bool TestLastTorque(float *torque)
{
    unsigned last = sTestRun.passes - 1U;
    if (!sTestRun.torqueSent[last])
    {
        return false;
    }
    *torque = sTestRun.torque[last];
    return true;
}

// Dernier ordre du voyant defaut pendant la marche : -1 aucun, 0 eteint, 1 allume
static int TestLastFaultLight(void)
{
    int state = -1;
    for (unsigned i = 0; (i < sTestRun.passes) && (i < TEST_MAX_PASSES); i++)
    {
        if (sTestRun.faultLight[i] != -1)
        {
            state = sTestRun.faultLight[i];
        }
    }
    return state;
}

#endif // TEST_HOST_TEST_VEHICLE_H
