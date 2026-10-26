#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "emu.h"

// Espion des queues : compte les messages deposes par les taches (xQueueSend), par queue et par
// identifiant de message. EmuTrace_QueueSend est appelee par le noyau (traceQUEUE_SEND, voir
// FreeRTOSConfig.h) en section critique : aucun appel FreeRTOS ici.

#define EMU_TRACE_MAX_ENTRIES 64U

typedef struct
{
    const void *queue;
    int messageId; // -1 : element qui n'est pas un dataMessage_t (mutex, ordres, watchdog...)
    uint32_t count;
    int32_t lastValue; // data du dernier message depose
} EmuTraceEntry_t;

static EmuTraceEntry_t sEntries[EMU_TRACE_MAX_ENTRIES];
static unsigned sEntryCount = 0U;

void EmuTrace_QueueSend(const void *queue, const void *item, unsigned long itemSize)
{
    int messageId = -1;
    int32_t value = 0;
    if ((item != NULL) && (itemSize == sizeof(dataMessage_t)))
    {
        dataMessage_t msg;
        memcpy(&msg, item, sizeof(msg));
        messageId = (int)msg.id;
        value = msg.data;
    }

    for (unsigned i = 0; i < sEntryCount; i++)
    {
        if ((sEntries[i].queue == queue) && (sEntries[i].messageId == messageId))
        {
            sEntries[i].count++;
            sEntries[i].lastValue = value;
            return;
        }
    }
    if (sEntryCount < EMU_TRACE_MAX_ENTRIES)
    {
        sEntries[sEntryCount].queue = queue;
        sEntries[sEntryCount].messageId = messageId;
        sEntries[sEntryCount].count = 1U;
        sEntries[sEntryCount].lastValue = value;
        sEntryCount++;
    }
}

bool EmuTrace_LastValue(QueueHandle_t queue, int messageId, int32_t *value)
{
    bool found = false;
    taskENTER_CRITICAL();
    for (unsigned i = 0; i < sEntryCount; i++)
    {
        if ((sEntries[i].queue == (const void *)queue) && (sEntries[i].messageId == messageId))
        {
            *value = sEntries[i].lastValue;
            found = true;
        }
    }
    taskEXIT_CRITICAL();
    return found;
}

uint32_t EmuTrace_SentCount(QueueHandle_t queue, int messageId)
{
    uint32_t count = 0U;
    taskENTER_CRITICAL();
    for (unsigned i = 0; i < sEntryCount; i++)
    {
        if ((sEntries[i].queue == (const void *)queue) && (sEntries[i].messageId == messageId))
        {
            count = sEntries[i].count;
        }
    }
    taskEXIT_CRITICAL();
    return count;
}
