
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

float GetUserThrottleCommand(dataMessage_t *pot1val, dataMessage_t *pot2val, int speed, bool brake)
{
    if (pot1val == NULL || pot2val == NULL)
    {
        console_print("Null pointer received in GetUserThrottleCommand\n");
        return 0.0f;
    }

    bool pot1TimeValid = (pot1val->timeStatus == STATUS_TIME_OK);
    bool pot2TimeValid = (pot2val->timeStatus == STATUS_TIME_OK);

    bool inRange1 = CheckAndLimitRange(&pot1val->data, 0);
    bool inRange2 = CheckAndLimitRange(&pot2val->data, 1);

    bool pot1Valid = pot1TimeValid && inRange1;
    bool pot2Valid = pot2TimeValid && inRange2;

    float pot1nom = NormalizeThrottle(pot1val->data, 0); ////normaliser pour avoir une valeure entre 0 et 100% en fonction de potmin
    float pot2nom = NormalizeThrottle(pot2val->data, 1); // et pot max

    // when there's something wrong with the dual throttle values,
    // we try to make the best of it and use the valid one
    if (pot1Valid && pot2Valid)
    {

        if (ABS(pot2nom - pot1nom) > 10.0f) // si la difference est trop grande, on prend la valeure la plus petite, et si elle est trop grande
        {                                   // on la met a 50%de potmax
            // utils::PostErrorIfRunning(ERR_THROTTLE12DIFF);

            // simple implementation of a limp mode: select the lower of
            // the two throttle inputs and limiting the throttle value
            // to 50%
            if (pot1nom < pot2nom)
            {
                pot1nom = MIN(pot1nom, 50.0f);
                return CalcThrottle(pot1nom, speed, brake);
            }
            else
            {
                pot2nom = MIN(pot2nom, 50.0f);
                return CalcThrottle(pot2nom, speed, brake);
            }
        }
        return CalcThrottle(pot1nom, speed, brake);
    }
    else if (pot1Valid && !pot2Valid)
    {
        // utils::PostErrorIfRunning(ERR_THROTTLE2);
        pot1nom = MIN(pot1nom, 50.0f);
        return CalcThrottle(pot1nom, speed, brake);
    }
    else if (!pot1Valid && pot2Valid)
    {
        // utils::PostErrorIfRunning(ERR_THROTTLE1);
        pot2nom = MIN(pot2nom, 50.0f);
        return CalcThrottle(pot2nom, speed, brake);
    }
    else // !pot1Valid && !pot2Valid
    {
        // utils::PostErrorIfRunning(ERR_THROTTLE12);
        return 0.0;
    }

    return 0.0;
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
        selectedValues[validCount] = wheelSpeeds[validCount].data;
        ++validCount;
    }

    sort4(selectedValues, validCount);

    switch (validCount)
    {
    case 4:
        speed = (selectedValues[1] + selectedValues[2]) >> 1;
        break;
    case 3:
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

/*float checkMessageTimeStamps(const MotorControlState_t *motorState, GlobalState_t *globalState, TickType_t time_now)
{
    // on ne retourne pas tout de suite pour que si on change les limites, on ne soit pas obliger de changer le code.(on pourrait retourner dans)
    // l'ordre croissant des limites, mais si on changes->il faut changer la structure du code)

    /////////// APPS /////////////////
    int limit = 100;
    if (time_now - motorState->appsValues[0].timestamp > MAX_APPS_TIMEOUT && time_now - motorState->appsValues[1].timestamp > MAX_APPS_TIMEOUT)
    {
        limit = MIN(limit, 0);
    }
    else if (time_now - motorState->appsValues[0].timestamp > MAX_APPS_TIMEOUT)
    {
        limit = MIN(limit, 50);
        globalState->derateReason |= DERATE_APPS1_OUTDATED;
    }
    else if (time_now - motorState->appsValues[1].timestamp > MAX_APPS_TIMEOUT)
    {
        limit = MIN(limit, 50);
        globalState->derateReason |= DERATE_APPS2_OUTDATED;
    }

    /////////// WheelSpeed /////////////////
    int nbOutdatedWheelValues = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (time_now - motorState->wheelSpeeds[i].timestamp > MAX_WHEEL_TIMEOUT)
        {
            globalState->derateReason |= DERATE_WHEEL_OUTDATED(i);
            nbOutdatedWheelValues++;
        }
    }
    if (nbOutdatedWheelValues == 4)
    {
        limit = MIN(limit, 0);
    }
    else if (nbOutdatedWheelValues == 3)
    {
        limit = MIN(limit, 40);
    }
    else if (nbOutdatedWheelValues >= 1)
    {
        limit = MIN(limit, 50);
    }

    /////////// Breakpedal /////////////////
    if (time_now - motorState->lastCheckedBreakPedal > MAX_BREAK_TIMEOUT)
    {
        limit = MIN(limit, 0);
        globalState->derateReason |= DERATE_BREAK_OUTDATED;
    }
}*/
