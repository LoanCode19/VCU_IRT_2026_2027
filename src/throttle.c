#include <stdbool.h>
#include "throttle.h"
#include "structs.h"
#include "utils.h"
#include "my_math.h"
#include "project_config.h"
#include "console.h"

#define POT_SLACK 200

int potmin[2];
int potmax[2];
float brknompedal;
float brkcruise;
float throtmax;
float throtmaxRev;
float throtmin;
float throtdead;
int idleSpeed;
int cruiseSpeed;
float speedkp;
int speedflt;
float idleThrotLim;
float regenRamp;
float actualThrottleRamp;
int bmslimhigh;
int bmslimlow;
int accelmax;
int accelflt;
float udcmin;
float udcmax;
float idcmin;
float idcmax;
int speedLimit;
float ThrotRpmFilt;

int speedFiltered;
float potnomFiltered;
float brkRamped;

float UDCres;
float IDCres;
float UDCprevspnt = 0;
float IDCprevspnt = 0;

float throttleRamped = 0.0;
float SpeedFiltered = 0.0; // limitation des saut de vitesse

#define PedalPosArrLen 50
float PedalPos;
float LastPedalPos;
float PedalChange = 0;
float PedalPosTot = 0;
float PedalPosArr[PedalPosArrLen];
uint8_t PedalPosIdx = 0;
int8_t PedalReq = 0; // positive is accel negative is decell

int throtRampRpm;
float throtRamp;
float throtRampMax;

float motorTempMax;
float inverterTempMax;

// Bornes de la course d'un APPS, dans l'ordre croissant : un capteur peut etre monte a l'envers
// (potmin > potmax)
static void AppsBounds(int potIdx, int *low, int *high)
{
    *low = (potmax[potIdx] > potmin[potIdx]) ? potmin[potIdx] : potmax[potIdx];
    *high = (potmax[potIdx] > potmin[potIdx]) ? potmax[potIdx] : potmin[potIdx];
}

// Valeur brute dans la fenetre acceptee : la course, elargie de POT_SLACK de chaque cote.
// Ne modifie jamais la valeur recue (S4) : un message garde sa valeur d'un cycle a l'autre.
bool AppsInRange(int32_t raw, int potIdx)
{
    int low;
    int high;
    AppsBounds(potIdx, &low, &high);
    return ((raw + POT_SLACK) >= low) && (raw <= (high + POT_SLACK));
}

// Valeur brute bornee a la course, pour le calcul du pourcentage
int32_t AppsClamp(int32_t raw, int potIdx)
{
    int low;
    int high;
    AppsBounds(potIdx, &low, &high);
    if (raw < low)
    {
        return low;
    }
    if (raw > high)
    {
        return high;
    }
    return raw;
}

/**
 * @brief Normalize the throttle input value to the min-max scale.
 *
 * Returns 0.0% for illegal indices and if potmin and potmax are equal.
 *
 * @param potval Throttle input value, range is [potmin[potIdx], potmax[potIdx]], not checked!
 * @param potIdx Index of the throttle input, should be [0, 1].
 * @return Normalized throttle value output with range [0.0, 100.0] with correct input.
 */
float NormalizeThrottle(int potval, int potIdx)
{
    if (potIdx < 0 || potIdx > 1)
        return 0.0f;

    if (potmin[potIdx] == potmax[potIdx])
        return 0.0f;

    return 100.0f * ((float)(potval - potmin[potIdx]) / (float)(potmax[potIdx] - potmin[potIdx]));
}

/**
 * @brief Calculate a throttle percentage from the potval input.
 *
 * After the previous range checks, the throttle input potval lies within the
 * range of [potmin[0], potmax[0]]. From this range, the input is converted to
 * a percent range of [-100.0, 100.0].
 *
 * Prerequisit: potval in range.

 *
 * TODO: No regen implemented. Commanding 0 throttle while braking, otherwise direct output.
 *
 * @param potVal throttle input between potmin and potmax
 * @param idx Index of the throttle input that should be used for calculation.
 * @param motorControlState struct containing all nessasry data for throttle calculation
 * @return float, value of throttle command between -100.0 and 100.0
 */
float CalcThrottle(float potnom, int speed, bool brake) /*int potval, int potIdx, bool brkpedal*/
{

    if (speed < 0) // make sure speed is not negative
    {
        speed *= -1;
    }

    // limiting speed change rate(pas d'accouts de vitesse)
    //  Si on recois des trames CAN éronnées, ou si il y as du bruit,les vcaleurs de speed recus seront fausses,donc les valeur de throttle calculés seront trop grands,
    // par rapport au précedante si speed ondule beaucou trop. c'est pour ca qu'on ramp le speed, meme si le couple calculé n'auras pas d'effet
    // ca evite de faire partie en vrille notre inverter a cause de bruit/glich CAN.
    if (ABS(speed - SpeedFiltered) > ThrotRpmFilt) /// max change is ThrotRpmFilt value
    {
        if (speed > SpeedFiltered)
        {
            SpeedFiltered += ThrotRpmFilt;
        }
        else
        {
            SpeedFiltered -= ThrotRpmFilt;
        }
    }
    else
    {
        SpeedFiltered = speed;
    }

    speed = SpeedFiltered;

    ///////////////////////

    // Frein appuye : couple 0, quelle que soit la pedale (S2, T 6.1.13). Pas de recuperation (D3) ; si elle
    // revient un jour, ce sera un couple negatif, et throtmin = 0 devra etre revu (S7). Le filtre de
    // vitesse ci-dessus a deja ete mis a jour : il continue de suivre.
    if (brake)
    {
        return 0.0f;
    }

    // substract offset, bring potval to the potmin-potmax scale and make a percentage
    // potnom = NormalizeThrottle(processedPotVal, processedPotValIdx); // to bring it between 0 and 100

    // Apply the deadzone parameter. To avoid that we lose the range between
    // 0 and throtdead, the scale of potnom is mapped from the [0.0, 100.0] scale
    // to the [throtdead, 100.0] scale.
    if (potnom < throtdead)
    {
        potnom = 0.0f; /// throtdead is the min accepted by the Inverter
    }
    else
    {
        potnom = (potnom - throtdead) * (100.0f / (100.0f - throtdead));
    }

    return potnom;
}

/**
 * @brief limits the throttle command if speed is too high
 *
 *
 * Prerequisit: potval in range.

 *
 * TODO: No regen implemented. Commanding 0 throttle while braking, otherwise direct output.
 *
 * @param finalSpnt calculated throttle between -100.0 ans 100.0
 * @param speed the current speed of the vehicule (taken from MotorState)
 * @return float, value of throttle command between -100.0 and 100.0
 */

void SpeedLimitCommand(float *finalSpnt, int speed)
{
    static int speedFiltered = 0;

    speedFiltered = IIRFILTER(speedFiltered, speed, 4);

    if (*finalSpnt > 0) // Only limit if driver is asking for positive torque (acceleration)
    {
        int speederr = speedLimit - speedFiltered; // How far below the speed limit we are
        int res = speederr / 4;                    // Scale the error down

        res = MAX(0, res);                 // Never allow negative (no throttle if over speed limit)
        *finalSpnt = MIN(res, *finalSpnt); // Clamp driver’s request down to res if necessary
    }
}

bool TemperatureDerate(int temp, int tempMax, float *finalSpnt)
{
    // uint16_t DerateReason = Param::GetInt(Param::TorqDerate);
    float limit = 0;

    if (temp <= tempMax)
    {
        limit = 100.0f;
    }
    else if (temp < (tempMax + 2))
    {
        limit = 50.0f;
        // DerateReason |= 16;
        // Param::SetInt(Param::TorqDerate, DerateReason);
    }

    if (*finalSpnt >= 0)
        *finalSpnt = MIN(*finalSpnt, limit);
    else
        *finalSpnt = MAX(*finalSpnt, -limit);

    return limit < 100.0f;
}

/**
 * @brief Apply the throttle ramping parameters for ramping up and down.
 *
 * @param potnom Normalized throttle command in percent, range [-100.0, 100.0].
 * @return float Ramped throttle command in percent, range [-100.0, 100.0].
 */
float RampThrottle(float potnom)
{
    // make sure potnom is within the boundaries of [throtmin, throtmax]
    potnom = MIN(potnom, throtmax);
    potnom = MAX(potnom, throtmin);

    if (potnom >= throttleRamped) // higher throttle command than currently applied
    {
        if (potnom > 0)
        {
            //                    //  current       target,  rate
            throttleRamped = RAMPUP(throttleRamped, potnom, actualThrottleRamp);
            potnom = throttleRamped;
        }
        else
        {
            throttleRamped = RAMPUP(throttleRamped, potnom, regenRamp);
            potnom = throttleRamped;
        }
    }
    else //(potnom < throttleRamped) // lower throttle command than currently applied
    {
        if (potnom >= 0)
        {
            throttleRamped = potnom; // No ramping from high throttle to low throttle
        }
        else
        {
            if (throttleRamped > 0)
            {
                throttleRamped = 0;
            }
            throttleRamped = RAMPDOWN(throttleRamped, potnom, regenRamp);
            potnom = throttleRamped;
        }
    }

    return potnom;
}
