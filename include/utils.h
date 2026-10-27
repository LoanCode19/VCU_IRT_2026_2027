#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "structs.h"

float GetUserThrottleCommand(const dataMessage_t *pot1val, const dataMessage_t *pot2val, int speed, bool brake);
bool AppsImplausible(const dataMessage_t *apps1, const dataMessage_t *apps2);
float AppsPedalPercent(const dataMessage_t *apps1);
void ResetThrottleState(void); // S9 (T10) : efface le verrou frein + accelerateur

// Surveillance des APPS dans TaskRawValuesProcessing (S1, D2)
typedef struct
{
    bool implausible; // incoherence en cours
    TickType_t since; // debut de l'incoherence
    bool latched;     // incoherence de plus de APPS_FAULT_CONFIRM_MS : faute confirmee
} AppsMonitor_t;

bool AppsMonitorStep(AppsMonitor_t *monitor, bool implausibleNow, float pedalPercent, TickType_t now);
float ProcessThrottle(float finalSpnt, float speedMs, int motorTemp, int inverterTemp, float voltage, uint32_t elapsedMs);
void updateDataTimeStatus(dataMessage_t *dataMsg, int count, int maxDiff, TickType_t now);
int32_t WheelRawToMmS(int32_t raw);
int calculateSpeed(dataMessage_t wheelSpeeds[4], int wheelCount);

// Dernier signe de vie de chaque tache surveillee, ecrit par wdgFeed() et lu par TaskWDG (T15).
// 0 : la tache n'a encore jamais donne signe de vie.
extern volatile TickType_t wdgLastFeed[MAX_TASKS];
void wdgFeed(TaskId taskId, TickType_t timestamp);
void wdgRegister(TaskId taskId, TickType_t timeout, WDGTimoutTable wdgTimeoutTable);

// Signe de vie d'une tache, a l'heure courante, a la fin de chaque tour de sa boucle. Sans watchdog (USE_WDG
// absent, comme dans la plupart des secteurs du banc), rien.
static inline void wdgFeedNow(TaskId taskId)
{
#ifdef USE_WDG
    wdgFeed(taskId, xTaskGetTickCount());
#else
    (void)taskId;
#endif
}

#endif // UTILS_H
