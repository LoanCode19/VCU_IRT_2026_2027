// Secteur : emission CAN
// Code teste : TaskCanTx (src/TaskCanTx.c) en vrai, avec les queues de MainApp(), et l'encodage des
// trames de l'onduleur DTI HV-550 (DTI CAN manual V2.5)
// Probleme : emission vers l'onduleur (lot 4, pas d'identifiant dans l'audit)

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "structs.h"
#include "canIds.h"
#include "queueHandles.h"
#include "my_fp.h"
#include "TaskCanTx.h"
#include "test_app.h"
#include "test_can.h"
#include "vcu_test.h"

static CanTxParam_t sCanTxParams; // static : jamais sur la pile (cf. B5)

static void Setup(void)
{
    BootMainApp();
    vTestCanReset();
    sCanTxParams = (CanTxParam_t){&xCanTxQueue, &xWDGQueue, TaskCanTxId};
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(100U);
}

// Consigne de Main vers CanTx, dans le format que TaskMain depose (pourcentage en virgule fixe)
static void PushTorqueCommand(float percent)
{
    dataMessage_t msg = {.id = inverterTorqueCmdId, .data = FP_FROMFLT(percent), .timestamp = 100U};
    (void)xQueueSend(xCanTxQueue, &msg, 0);
}

static void Describe(char *out, size_t size, bool sent, const CanFrame_t *frame)
{
    if (!sent)
    {
        (void)snprintf(out, size, "non emise");
        return;
    }
    (void)snprintf(out, size, "DLC %u, %02X %02X", (unsigned)frame->dlc, frame->data[0], frame->data[1]);
}

static void TestCanTx_TorqueFrame(void)
{
    Setup();
    PushTorqueCommand(30.0f);
    RunTaskOnce(TaskCanTx, &sCanTxParams);

    CanFrame_t frame = {0};
    bool sent = xTestCanLastTx(CAN_ID_DTI_SET_REL_CURRENT, &frame);
    bool ok = sent && (frame.ide == 0U) && (frame.dlc == 2U) && (frame.data[0] == 0x01U) && (frame.data[1] == 0x2CU);
    char got[40];
    Describe(got, sizeof(got), sent, &frame);
    vt_check("TX-d", "-", ok, "consigne de 30 %% -> trame 0x%03lX : %s (attendu DLC 2, 01 2C = 300 pour 30,0 %%)",
             (unsigned long)CAN_ID_DTI_SET_REL_CURRENT, got);
}

int main(void)
{
    vt_begin("Emission CAN");

    TestCanTx_TorqueFrame();

    return vt_end();
}
