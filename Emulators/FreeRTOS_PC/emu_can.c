#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "canIds.h"
#include "queueHandles.h"
#include "TaskCanRX.h"
#include "emu.h"

#define EMU_CAN_STACK_DEPTH 256U

typedef struct
{
    bool used;
    bool muted;
    uint32_t stdId;
    uint32_t periodMs;
    uint8_t dlc;
    uint8_t data[8];
    TickType_t nextSend;
} EmuCanSignal_t;

static EmuCanSignal_t sSignals[EMU_CAN_MAX_SIGNALS];

#define EMU_CAN_MAX_TX_IDS 8U

typedef struct
{
    bool used;
    uint32_t count;
    CanFrame_t last;
} EmuCanTx_t;

static EmuCanTx_t sTxFrames[EMU_CAN_MAX_TX_IDS];
static volatile uint32_t sDroppedFrames = 0;

static StaticTask_t sBusTaskTcb;
static StackType_t sBusTaskStack[EMU_CAN_STACK_DEPTH];

// Comme l'ISR de reception de la carte : la trame est remise a CanRx_OnFrameFromISR(), le meme
// point d'entree que sur la carte. L'appelant est une tache (bus CAN ou test) : la section critique
// joue le role de l'interruption, aucune tache ne peut s'intercaler pendant le depot.
static bool EmuCan_DeliverFrame(uint32_t stdId, uint8_t dlc, const uint8_t data[8])
{
    CanFrame_t frame = {0};
    frame.id = stdId;
    frame.ide = 0U;
    frame.dlc = dlc;
    memcpy(frame.data, data, sizeof(frame.data));
    frame.rxTick = xTaskGetTickCount();

    BaseType_t woken = pdFALSE;
    taskENTER_CRITICAL();
    bool full = (uxQueueSpacesAvailable(xCanRxQueue) == 0U);
    CanRx_OnFrameFromISR(&frame, &woken);
    taskEXIT_CRITICAL();
    if (woken == pdTRUE)
    {
        taskYIELD();
    }
    if (full)
    {
        sDroppedFrames++; // trame perdue (ou qui a pris la place d'une plus ancienne)
    }
    return !full;
}

// Format provisoire des capteurs : la valeur sur les 4 premiers octets, big-endian
static void EncodeValue(int32_t value, uint8_t data[8])
{
    memset(data, 0, 8U);
    data[0] = (uint8_t)((uint32_t)value >> 24);
    data[1] = (uint8_t)((uint32_t)value >> 16);
    data[2] = (uint8_t)((uint32_t)value >> 8);
    data[3] = (uint8_t)value;
}

bool EmuCan_SendFrame(uint32_t stdId, uint8_t dlc, const uint8_t data[8])
{
    return EmuCan_DeliverFrame(stdId, dlc, data);
}

bool EmuCan_SendNow(uint32_t stdId, int32_t value)
{
    uint8_t data[8];
    EncodeValue(value, data);
    return EmuCan_DeliverFrame(stdId, 4U, data);
}

static EmuCanSignal_t *FindSignal(uint32_t stdId, bool create)
{
    EmuCanSignal_t *freeSlot = NULL;
    for (unsigned i = 0; i < EMU_CAN_MAX_SIGNALS; i++)
    {
        if (sSignals[i].used && (sSignals[i].stdId == stdId))
        {
            return &sSignals[i];
        }
        if (!sSignals[i].used && (freeSlot == NULL))
        {
            freeSlot = &sSignals[i];
        }
    }
    if (create && (freeSlot != NULL))
    {
        freeSlot->used = true;
        freeSlot->stdId = stdId;
        return freeSlot;
    }
    return NULL;
}

void EmuCan_SetPeriodicFrame(uint32_t stdId, uint32_t periodMs, uint8_t dlc, const uint8_t data[8])
{
    taskENTER_CRITICAL();
    EmuCanSignal_t *signal = FindSignal(stdId, true);
    configASSERT(signal != NULL); // plus de EMU_CAN_MAX_SIGNALS capteurs simules
    signal->periodMs = (periodMs == 0U) ? 1U : periodMs;
    signal->dlc = dlc;
    memcpy(signal->data, data, sizeof(signal->data));
    signal->muted = false;
    signal->nextSend = xTaskGetTickCount();
    taskEXIT_CRITICAL();
}

void EmuCan_SetPeriodic(uint32_t stdId, uint32_t periodMs, int32_t value)
{
    uint8_t data[8];
    EncodeValue(value, data);
    EmuCan_SetPeriodicFrame(stdId, periodMs, 4U, data);
}

void EmuCan_SetValue(uint32_t stdId, int32_t value)
{
    uint8_t data[8];
    EncodeValue(value, data);
    taskENTER_CRITICAL();
    EmuCanSignal_t *signal = FindSignal(stdId, false);
    if (signal != NULL)
    {
        memcpy(signal->data, data, sizeof(signal->data));
    }
    taskEXIT_CRITICAL();
}

void EmuCan_Mute(uint32_t stdId, bool muted)
{
    taskENTER_CRITICAL();
    EmuCanSignal_t *signal = FindSignal(stdId, false);
    if (signal != NULL)
    {
        signal->muted = muted;
        signal->nextSend = xTaskGetTickCount();
    }
    taskEXIT_CRITICAL();
}

void EmuCan_StopAll(void)
{
    taskENTER_CRITICAL();
    for (unsigned i = 0; i < EMU_CAN_MAX_SIGNALS; i++)
    {
        sSignals[i].used = false;
    }
    taskEXIT_CRITICAL();
}

uint32_t EmuCan_DroppedFrames(void)
{
    return sDroppedFrames;
}

// Emission (stm32Interface.h) : sur la carte, la trame part sur le bus ; ici, elle est enregistree
bool CanTx_Send(const CanFrame_t *frame)
{
    taskENTER_CRITICAL();
    EmuCanTx_t *slot = NULL;
    for (unsigned i = 0; (i < EMU_CAN_MAX_TX_IDS) && (slot == NULL); i++)
    {
        if (sTxFrames[i].used && (sTxFrames[i].last.id == frame->id))
        {
            slot = &sTxFrames[i];
        }
    }
    for (unsigned i = 0; (i < EMU_CAN_MAX_TX_IDS) && (slot == NULL); i++)
    {
        if (!sTxFrames[i].used)
        {
            slot = &sTxFrames[i];
            slot->used = true;
            slot->count = 0U;
        }
    }
    if (slot != NULL)
    {
        slot->count++;
        slot->last = *frame;
    }
    taskEXIT_CRITICAL();
    return true;
}

uint32_t EmuCan_TxCount(uint32_t stdId)
{
    uint32_t count = 0U;
    taskENTER_CRITICAL();
    for (unsigned i = 0; i < EMU_CAN_MAX_TX_IDS; i++)
    {
        if (sTxFrames[i].used && (sTxFrames[i].last.id == stdId))
        {
            count = sTxFrames[i].count;
        }
    }
    taskEXIT_CRITICAL();
    return count;
}

bool EmuCan_LastTx(uint32_t stdId, CanFrame_t *frame)
{
    bool found = false;
    taskENTER_CRITICAL();
    for (unsigned i = 0; i < EMU_CAN_MAX_TX_IDS; i++)
    {
        if (sTxFrames[i].used && (sTxFrames[i].last.id == stdId))
        {
            *frame = sTxFrames[i].last;
            found = true;
        }
    }
    taskEXIT_CRITICAL();
    return found;
}

// Onduleur simule (trames du DTI, big-endian) : temperatures en 0,1 degre, octets 0-1 onduleur, 2-3 moteur
void EmuDti_SetTemperatures(int32_t motorC, int32_t inverterC, uint32_t periodMs)
{
    uint8_t data[8] = {0, 0, 0, 0, 0x00, 0xFF, 0xFF, 0xFF}; // octet 4 : pas de defaut
    uint32_t inverterRaw = (uint32_t)(inverterC * 10);
    uint32_t motorRaw = (uint32_t)(motorC * 10);
    data[0] = (uint8_t)(inverterRaw >> 8);
    data[1] = (uint8_t)inverterRaw;
    data[2] = (uint8_t)(motorRaw >> 8);
    data[3] = (uint8_t)motorRaw;
    EmuCan_SetPeriodicFrame(CAN_ID_DTI_TEMPERATURES, periodMs, 8U, data);
}

// Onduleur simule : tension DC en volts, octets 6-7 de la trame ERPM / rapport cyclique / tension
void EmuDti_SetVoltage(int32_t volts, uint32_t periodMs)
{
    uint8_t data[8] = {0};
    data[6] = (uint8_t)((uint32_t)volts >> 8);
    data[7] = (uint8_t)volts;
    EmuCan_SetPeriodicFrame(CAN_ID_DTI_ERPM_VOLTAGE, periodMs, 8U, data);
}

// Toutes les 1 ms : envoie les trames des capteurs simules arrivees a echeance
static void EmuCan_BusTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t lastWake = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1));
        TickType_t now = xTaskGetTickCount();

        for (unsigned i = 0; i < EMU_CAN_MAX_SIGNALS; i++)
        {
            taskENTER_CRITICAL();
            EmuCanSignal_t signal = sSignals[i];
            bool due = signal.used && !signal.muted && ((TickType_t)(now - signal.nextSend) < (TickType_t)0x80000000u);
            if (due)
            {
                sSignals[i].nextSend = signal.nextSend + pdMS_TO_TICKS(signal.periodMs);
            }
            taskEXIT_CRITICAL();

            if (due)
            {
                (void)EmuCan_DeliverFrame(signal.stdId, signal.dlc, signal.data);
            }
        }
    }
}

void EmuCan_Init(void)
{
    // Appelee avant le demarrage du scheduler : pas besoin de section critique
    sDroppedFrames = 0;
    for (unsigned i = 0; i < EMU_CAN_MAX_SIGNALS; i++)
    {
        sSignals[i].used = false;
    }
    for (unsigned i = 0; i < EMU_CAN_MAX_TX_IDS; i++)
    {
        sTxFrames[i].used = false;
    }
    (void)xTaskCreateStatic(EmuCan_BusTask, "EMU_CAN", EMU_CAN_STACK_DEPTH, NULL,
                            configMAX_PRIORITIES - 1, sBusTaskStack, &sBusTaskTcb);
}
