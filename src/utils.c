
#include <stdbool.h>
#include "throttle.h"
#include "structs.h"
#include "utils.h"
#include "my_math.h"
#include "console.h"
#include "project_config.h"

#include "FreeRTOS.h"

#define MAX_APPS_TIMEOUT 200
#define MAX_WHEEL_TIMEOUT 400
#define MAX_BREAK_TIMEOUT 150

#define MAX_DIFF_WHEELS 30

// Incoherence des APPS (S1, T 11.8.8, T 11.8.9, T 11.9.2) : un capteur absent ou en retard, hors plage,
// ou plus de APPS_MAX_DIFF_PERCENT points d'ecart entre les deux
bool AppsImplausible(const dataMessage_t *apps1, const dataMessage_t *apps2)
{
    if ((apps1->timeStatus != STATUS_TIME_OK) || (apps2->timeStatus != STATUS_TIME_OK) ||
        !AppsInRange(apps1->data, 0) || !AppsInRange(apps2->data, 1))
    {
        return true;
    }
    float pot1nom = NormalizeThrottle(AppsClamp(apps1->data, 0), 0);
    float pot2nom = NormalizeThrottle(AppsClamp(apps2->data, 1), 1);
    return ABS(pot2nom - pot1nom) > APPS_MAX_DIFF_PERCENT;
}

// Position de la pedale, en % de la course, lue sur APPS1 : c'est elle qui donne le couple
float AppsPedalPercent(const dataMessage_t *apps1)
{
    return NormalizeThrottle(AppsClamp(apps1->data, 0), 0);
}

float GetUserThrottleCommand(const dataMessage_t *pot1val, const dataMessage_t *pot2val, int speed, bool brake)
{
    if (pot1val == NULL || pot2val == NULL)
    {
        console_print("Null pointer received in GetUserThrottleCommand\n");
        return 0.0f;
    }

    // Aucun mode degrade : couple coupe des le premier cycle incoherent, plus tot que les 100 ms
    // permis par T 11.8.8. La confirmation de la faute est faite par AppsMonitorStep(), dans TRVP.
    if (AppsImplausible(pot1val, pot2val))
    {
        return 0.0f;
    }
    return CalcThrottle(AppsPedalPercent(pot1val), speed, brake);
}

// Une incoherence qui dure plus de APPS_FAULT_CONFIRM_MS est confirmee : le couple reste coupe meme
// quand les APPS redeviennent coherents, jusqu'a ce que la pedale repasse sous
// APPS_FAULT_RELEASE_PERCENT (D2). Renvoie true si le couple est autorise ce cycle.
bool AppsMonitorStep(AppsMonitor_t *monitor, bool implausibleNow, float pedalPercent, TickType_t now)
{
    if (implausibleNow)
    {
        if (!monitor->implausible)
        {
            monitor->implausible = true;
            monitor->since = now;
        }
        if ((TickType_t)(now - monitor->since) > pdMS_TO_TICKS(APPS_FAULT_CONFIRM_MS))
        {
            monitor->latched = true;
        }
        return false;
    }
    monitor->implausible = false;
    if (monitor->latched && (pedalPercent < APPS_FAULT_RELEASE_PERCENT))
    {
        monitor->latched = false;
    }
    return !monitor->latched;
}

float ProcessThrottle(float finalSpnt, int speed, int motorTemp, int inverterTemp, float voltage)
{
    // we claculate messageTimouts here to minimize the probability of a context switch that would make the time_now
    // not reliable. If Max timouts are big enough, that should not pose any problem.
    // float limitMsgTimeouts = checkMessageTimeStamps(motorState, globalState, time_now);

    if (speed < throtRampRpm)
    {
        actualThrottleRamp = throtRamp;
    }
    else
    {
        actualThrottleRamp = throtRampMax;
    }

    // Throttle::UdcLimitCommand(finalSpnt,Variable             s::GetFloat(Variables::udc));
    // Throttle::IdcLimitCommand(finalSpnt, ABS(Variables::GetFloat(Variables::idc)));
    SpeedLimitCommand(&finalSpnt, ABS(speed));

    if (TemperatureDerate(motorTemp, motorTempMax, &finalSpnt))
    {
        console_print("derating becouse of motor temprature\n");
    }

    if (TemperatureDerate(inverterTemp, inverterTempMax, &finalSpnt))
    {
        console_print("derating because of inverter temprature\n");
    }

    finalSpnt = RampThrottle(finalSpnt);

    // make sure the torque percentage is NEVER out of range
    if (finalSpnt < -100.0f)
        finalSpnt = -100.0f;
    else if (finalSpnt > 100.0f)
        finalSpnt = 100.0f;

    return finalSpnt;
}

int calculateSpeed(dataMessage_t wheelSpeeds[4], int wheelCount)
{

    // Simple average of the four wheel speeds
    int32_t selectedValues[4];
    uint32_t speed = 0;
    int validCount = 0;

    for (int i = 0; i < wheelCount; ++i)
    {
        if (wheelSpeeds[i].timeStatus != STATUS_TIME_OK)
        {
            continue; // Skip this wheel if its data is not OK
        }
        selectedValues[validCount] = wheelSpeeds[i].data;
        ++validCount;
    }

    sort4(selectedValues, validCount);

    switch (validCount)
    {
    case 4:
        speed = (selectedValues[1] + selectedValues[2]) >> 1;
        break;
    case 3:
    {
        int d01 = ABS(selectedValues[0] - selectedValues[1]);
        int d12 = ABS(selectedValues[1] - selectedValues[2]);
        int d02 = ABS(selectedValues[0] - selectedValues[2]);

        if (d01 < MAX_DIFF_WHEELS && d12 < MAX_DIFF_WHEELS)
        {
            speed = (selectedValues[0] + selectedValues[1] + selectedValues[2]) / 3;
        }
        else if (d01 < MAX_DIFF_WHEELS)
        {
            speed = (selectedValues[0] + selectedValues[1]) >> 1;
        }
        else if (d12 < MAX_DIFF_WHEELS)
        {
            speed = (selectedValues[1] + selectedValues[2]) >> 1;
        }
        else
        {
            speed = 0;
        }

        break;
    }
    case 2:
        if (ABS(selectedValues[0] - selectedValues[1]) > MAX_DIFF_WHEELS) // If the two values are too different, we consider them unreliable
        {
            speed = 0;
        }
        else
        {
            speed = (selectedValues[0] + selectedValues[1]) >> 1; // Average of the two values
        }
        break;

    case 1:
        speed = selectedValues[0]; // Only one valid value, use it as speed
        break;
    default:
        speed = 0; // No valid values, set speed to 0
        break;
    }
    return speed;
}

bool checkWheelSpeedsInRange(dataMessage_t wheelSpeed)
{
    return 1;
}

/** This function checks the timestamps of the received data messages and updates their time status accordingly.
 *
 * @param dataMsg An array or single data message to check.
 * @param count The number of data messages in the array.
 * @param now The current timestamp.
 */
void updateDataTimeStatus(dataMessage_t *dataMsg, int count, int maxDiff, TickType_t now)
{
    for (int i = 0; i < count; i++)
    {
        if (dataMsg[i].timeStatus == STATUS_NOT_READY)
        {
            continue;
        }
        TickType_t timeDiff = now - dataMsg[i].timestamp;

        if (timeDiff > pdMS_TO_TICKS(maxDiff))
        {
            dataMsg[i].timeStatus = STATUS_TIMEOUT;
        }
    }
}

void sort4(int32_t v[4], uint8_t n)
{
    switch (n)
    {
    case 4:
        swap(&v[0], &v[1]);
        swap(&v[2], &v[3]);
        swap(&v[0], &v[2]);
        swap(&v[1], &v[3]);
        swap(&v[1], &v[2]);
        break;

    case 3:
        swap(&v[0], &v[1]);
        swap(&v[1], &v[2]);
        swap(&v[0], &v[1]);
        break;

    case 2:
        swap(&v[0], &v[1]);
        break;

    case 1:
    case 0:
    default:
        // rien à faire
        break;
    }
}

void wdgRegister(TaskId taskId, TickType_t timeout, WDGTimoutTable wdgTimeoutTable)
{
    if (taskId < MAX_TASKS)
    {
        wdgTimeoutTable[taskId] = timeout;
    }
}

// Une case par tache, a la place d'une queue : un signe de vie ne se perd jamais, meme quand les
// taches rapides tournent a chaque trame CAN, et n'attend jamais (W6, T15). Ecrire un mot de 32 bits
// aligne est atomique sur le Cortex-M4 : pas besoin de section critique.
volatile TickType_t wdgLastFeed[MAX_TASKS];

void wdgFeed(TaskId taskId, TickType_t timestamp)
{
    if ((unsigned)taskId < (unsigned)MAX_TASKS) // en non signe : rejette aussi les valeurs negatives
    {
        wdgLastFeed[taskId] = timestamp;
    }
}