
#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"
#include "canIds.h"
#include "project_config.h"
#include "stm32Interface.h"
#include "utils.h"

#include "console.h"
#include "TaskCanTx.h"
#include "my_fp.h"

volatile uint32_t canTxErrors = 0;

// Consigne de couple pour l'onduleur DTI : commande 0x05 "Set relative current", en % des limites
// reglees dans DTI CAN Tool, x10 sur 2 octets signes big-endian. Bornee de 0 a 100 % : jamais de
// couple negatif (D3), et une valeur invalide (NaN) donne 0.
CanFrame_t Dti_EncodeRelativeCurrent(float percent)
{
    int16_t raw = 0;
    if (!(percent > 0.0f)) // negatif, nul ou NaN
    {
        raw = 0;
    }
    else if (percent >= 100.0f)
    {
        raw = 1000;
    }
    else
    {
        raw = (int16_t)((percent * 10.0f) + 0.5f);
    }

    CanFrame_t frame = {.id = CAN_ID_DTI_SET_REL_CURRENT, .ide = 0U, .dlc = 2U};
    frame.data[0] = (uint8_t)((uint16_t)raw >> 8);
    frame.data[1] = (uint8_t)raw;
    return frame;
}

// Commande 0x0C "Drive enable" : 1 = conduite autorisee, 0 = interdite. Main l'envoie a chaque periode, 1 en R2D
// (T20).
CanFrame_t Dti_EncodeDriveEnable(bool enable)
{
    CanFrame_t frame = {.id = CAN_ID_DTI_DRIVE_ENABLE, .ide = 0U, .dlc = 1U};
    frame.data[0] = enable ? 1U : 0U;
    return frame;
}

// Trame de l'onduleur pour un message de Main. false : message que la tache n'attend pas (ignore).
static bool EncodeCommand(const dataMessage_t *msg, CanFrame_t *frame)
{
    switch (msg->id)
    {
    case inverterTorqueCmdId:
        *frame = Dti_EncodeRelativeCurrent(FP_TOFLOAT(msg->data));
        return true;
    case driveEnableCmdId:
        *frame = Dti_EncodeDriveEnable(msg->data != 0);
        return true;
    default:
        return false;
    }
}

void TaskCanTx(void *pvParameters)
{
    CanTxParam_t *params = (CanTxParam_t *)pvParameters;
    QueueHandle_t *xCanTxQueue = params->xCanTxQueue;
    TaskId taskId = params->taskId;

    while (1)
    {
        dataMessage_t msg = {0};
        CanFrame_t frame;
        if ((xQueueReceive(*xCanTxQueue, &msg, CAN_TX_TIMEOUT) == pdPASS) && EncodeCommand(&msg, &frame))
        {
            if (!CanTx_Send(&frame))
            {
                canTxErrors++; // boites d'emission pleines : la trame suivante partira
            }
        }

        wdgFeedNow(taskId);
    }
}
