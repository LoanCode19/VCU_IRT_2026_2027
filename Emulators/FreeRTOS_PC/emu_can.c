#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "queueHandles.h"
#include "emu.h"

#define EMU_CAN_STACK_DEPTH 256U

typedef struct
{
    bool used;
    bool muted;
    uint32_t stdId;
    uint32_t periodMs;
    int32_t value;
    TickType_t nextSend;
} EmuCanSignal_t;

static EmuCanSignal_t sSignals[EMU_CAN_MAX_SIGNALS];
static volatile uint32_t sDroppedFrames = 0;

static StaticTask_t sBusTaskTcb;
static StackType_t sBusTaskStack[EMU_CAN_STACK_DEPTH];

// Copie de HAL_CAN_RxFifo0MsgPendingCallback (VCU_STM32F407/Core/Src/main.c) : meme conversion,
// meme queue. L'ISR est remplacee par la tache "bus CAN", de priorite maximale.
static bool EmuCan_DeliverFrame(uint32_t stdId, const uint8_t data[8])
{
    dataCan canMsg = {0};
    canMsg.id = stdId; // tronque comme dans l'ISR (dataCan.id est un uint8_t, probleme B6)
    canMsg.data = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    if (xQueueSend(xCanRxQueue, &canMsg, 0) != pdPASS)
    {
        sDroppedFrames++;
        return false;
    }
    return true;
}

bool EmuCan_SendNow(uint32_t stdId, int32_t value)
{
    uint8_t data[8] = {0};
    data[0] = (uint8_t)((uint32_t)value >> 24);
    data[1] = (uint8_t)((uint32_t)value >> 16);
    data[2] = (uint8_t)((uint32_t)value >> 8);
    data[3] = (uint8_t)value;
    return EmuCan_DeliverFrame(stdId, data);
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

void EmuCan_SetPeriodic(uint32_t stdId, uint32_t periodMs, int32_t value)
{
    taskENTER_CRITICAL();
    EmuCanSignal_t *signal = FindSignal(stdId, true);
    configASSERT(signal != NULL); // plus de EMU_CAN_MAX_SIGNALS capteurs simules
    signal->periodMs = (periodMs == 0U) ? 1U : periodMs;
    signal->value = value;
    signal->muted = false;
    signal->nextSend = xTaskGetTickCount();
    taskEXIT_CRITICAL();
}

void EmuCan_SetValue(uint32_t stdId, int32_t value)
{
    taskENTER_CRITICAL();
    EmuCanSignal_t *signal = FindSignal(stdId, false);
    if (signal != NULL)
    {
        signal->value = value;
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
                (void)EmuCan_SendNow(signal.stdId, signal.value);
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
    (void)xTaskCreateStatic(EmuCan_BusTask, "EMU_CAN", EMU_CAN_STACK_DEPTH, NULL,
                            configMAX_PRIORITIES - 1, sBusTaskStack, &sBusTaskTcb);
}
