
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

// Commande 0x0C "Drive enable" : 1 = conduite autorisee, 0 = interdite. Envoyee par la machine R2D (lot 6).
CanFrame_t Dti_EncodeDriveEnable(bool enable)
{
    CanFrame_t frame = {.id = CAN_ID_DTI_DRIVE_ENABLE, .ide = 0U, .dlc = 1U};
    frame.data[0] = enable ? 1U : 0U;
    return frame;
}

void TaskCanTx(void *pvParameters)
{
    TickType_t lastWakeUp = xTaskGetTickCount();
    unsigned long i = 0;
    dataMessage_t msg = {0};

    CanTxParam_t *params = (CanTxParam_t *)pvParameters;
    QueueHandle_t *xCanTxQueue = params->xCanTxQueue;
    TaskId taskId = params->taskId;

    while (1)
    {

        msg.id++;
        msg.timestamp = xTaskGetTickCount();

        dataMessage_t msg = {0};
        BaseType_t xReturn = xQueueReceive(*xCanTxQueue, &msg, CAN_TX_TIMEOUT);
        if (xReturn == pdPASS)
        {

            switch (msg.id)
            {
            case inverterTorqueCmdId:
            {
                float throttle = FP_TOFLOAT(msg.data);
                CanFrame_t frame = Dti_EncodeRelativeCurrent(throttle);
                if (!CanTx_Send(&frame))
                {
                    canTxErrors++; // boites d'emission pleines : la consigne suivante partira
                }
                break;
            }
            }
        }

#ifdef USE_WDG
        // feed WDG
        wdgFeed(taskId, xTaskGetTickCount());
#endif
    }
}