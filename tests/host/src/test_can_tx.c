// Secteur : emission CAN
// Code teste : TaskCanTx (src/TaskCanTx.c) en vrai, avec les queues de MainApp(), et l'encodage des
// trames de l'onduleur DTI HV-550 (DTI CAN manual V2.5)
// Probleme : emission vers l'onduleur (lot 4, pas d'identifiant dans l'audit)

#include <math.h>
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

static int RawPercent(const CanFrame_t *frame)
{
    return (int16_t)(((uint16_t)frame->data[0] << 8) | frame->data[1]);
}

static void TestCanTx_Encoding(void)
{
    // 55,5 % : octet de poids fort non nul, pour verifier l'ordre des octets (big-endian)
    CanFrame_t f = Dti_EncodeRelativeCurrent(25.0f);
    CanFrame_t g = Dti_EncodeRelativeCurrent(55.5f);
    vt_check("TX-a", "-",
             (f.id == CAN_ID_DTI_SET_REL_CURRENT) && (f.ide == 0U) && (f.dlc == 2U) && (f.data[0] == 0x00U) &&
                 (f.data[1] == 0xFAU) && (g.data[0] == 0x02U) && (g.data[1] == 0x2BU),
             "consignes de 25 %% et 55,5 %% -> ID 0x%03lX, DLC %u, %02X %02X et %02X %02X (attendu 0x0A4, DLC 2, "
             "00 FA et 02 2B)",
             (unsigned long)f.id, (unsigned)f.dlc, f.data[0], f.data[1], g.data[0], g.data[1]);

    CanFrame_t negative = Dti_EncodeRelativeCurrent(-5.0f);
    CanFrame_t over = Dti_EncodeRelativeCurrent(120.0f);
    CanFrame_t invalid = Dti_EncodeRelativeCurrent(NAN);
    vt_check("TX-b", "-", (RawPercent(&negative) == 0) && (RawPercent(&over) == 1000) && (RawPercent(&invalid) == 0),
             "consignes de -5 %%, 120 %%, NaN -> %d, %d, %d (attendu 0, 1000, 0 : jamais negative (D3), bornee a 100 %%)",
             RawPercent(&negative), RawPercent(&over), RawPercent(&invalid));

    CanFrame_t on = Dti_EncodeDriveEnable(true);
    CanFrame_t off = Dti_EncodeDriveEnable(false);
    vt_check("TX-c", "-",
             (on.id == CAN_ID_DTI_DRIVE_ENABLE) && (on.dlc == 1U) && (on.data[0] == 1U) &&
                 (off.id == CAN_ID_DTI_DRIVE_ENABLE) && (off.data[0] == 0U),
             "drive enable vrai / faux -> ID 0x%03lX, octet 0 = %u / %u (attendu 0x184, 1 / 0)", (unsigned long)on.id,
             (unsigned)on.data[0], (unsigned)off.data[0]);
}

static void TestCanTx_SendFailure(void)
{
    Setup();
    uint32_t before = canTxErrors;
    vTestCanSetTxFails(true);
    PushTorqueCommand(30.0f);
    RunTaskOnce(TaskCanTx, &sCanTxParams);
    vTestCanSetTxFails(false);
    unsigned long errors = (unsigned long)(canTxErrors - before);
    vt_check("TX-e", "-", errors == 1UL, "emission refusee (boites d'emission pleines) -> canTxErrors +%lu (attendu 1)",
             errors);
}

int main(void)
{
    vt_begin("Emission CAN");

    TestCanTx_Encoding();
    TestCanTx_TorqueFrame();
    TestCanTx_SendFailure();

    return vt_end();
}
