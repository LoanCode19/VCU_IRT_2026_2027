
#include "FreeRTOS.h"
#include "structs.h"

float GetUserThrottleCommand(const dataMessage_t *pot1val, const dataMessage_t *pot2val, int speed, bool brake);
bool AppsImplausible(const dataMessage_t *apps1, const dataMessage_t *apps2);
float AppsPedalPercent(const dataMessage_t *apps1);

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
void sort4(int32_t v[4], uint8_t n);
int32_t WheelRawToMmS(int32_t raw);
int calculateSpeed(dataMessage_t wheelSpeeds[4], int wheelCount);
// Dernier signe de vie de chaque tache surveillee, ecrit par wdgFeed() et lu par TaskWDG (T15).
// 0 : la tache n'a encore jamais donne signe de vie.
extern volatile TickType_t wdgLastFeed[MAX_TASKS];
void wdgFeed(TaskId taskId, TickType_t timestamp);
void wdgRegister(TaskId taskId, TickType_t timeout, WDGTimoutTable wdgTimeoutTable);
//  float checkMessageTimeStamps(const MotorControlState_t *motorState, GlobalState_t *globalState, TickType_t time_now);

inline static int32_t change(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline static float changeFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline static void swap(int32_t *a, int32_t *b)
{
    if (*a > *b)
    {
        int32_t tmp = *a;
        *a = *b;
        *b = tmp;
    }
}
