#include <stdbool.h>
#include "throttle.h"
#include "structs.h"
#include "utils.h"
#include "my_math.h"
#include "project_config.h"
#include "console.h"

#define POT_SLACK 200

// Calibration, reglee par initThrottleValues() (VCU.c)
int potmin[2];
int potmax[2];
float throtmax;
float throtmin;
float throtdead;
float udcmin;
float udcDerateMarginV;
float speedLimitMs;
float speedLimitMarginMs;
float ThrotRpmFilt;
float throtRampSpeedMs;
float throtRampPerS;
float throtRampMaxPerS;
float motorTempMax;
float inverterTempMax;

// Etat d'un passage a l'autre (remis a zero par les tests)
float throttleRamped = 0.0f; // consigne apres la rampe, en %
float SpeedFiltered = 0.0f;  // vitesse filtree : limite les sauts de vitesse

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
 * @brief Demande du pilote, en %, a partir de la position de la pedale : 0 si le frein est appuye, puis la
 * zone morte throtdead. Pas de recuperation (D3) : jamais de valeur negative.
 *
 * @param potnom Position de la pedale, en % de la course (AppsPedalPercent()).
 * @param speed Vitesse du vehicule, en mm/s : seulement filtree ici, pour suivre la voiture.
 * @param brake Frein appuye, ou signal du frein douteux (S2).
 * @return float Demande de 0 a 100 %.
 */
float CalcThrottle(float potnom, int speed, bool brake)
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
 * @brief Limiteur de vitesse (B3), en m/s (D7, T12) : aucune limite sous speedLimitMs - speedLimitMarginMs,
 * puis une reduction lineaire jusqu'a 0 a speedLimitMs. Sans filtre : la vitesse est deja la mediane des roues.
 *
 * @param finalSpnt consigne en %, reduite sur place si elle est positive
 * @param speedMs vitesse du vehicule, en m/s
 */
void SpeedLimitCommand(float *finalSpnt, float speedMs)
{
    if (*finalSpnt <= 0.0f) // seule une demande de couple positif est limitee
    {
        return;
    }
    if (speedMs >= speedLimitMs)
    {
        *finalSpnt = 0.0f;
        return;
    }
    if (speedMs > speedLimitMs - speedLimitMarginMs)
    {
        float maxAllowed = 100.0f * (speedLimitMs - speedMs) / speedLimitMarginMs;
        *finalSpnt = MIN(*finalSpnt, maxAllowed);
    }
}

/**
 * @brief Reduction sur tension batterie basse (S8, T11) : rien au-dessus de udcmin + udcDerateMarginV, puis une
 * baisse lineaire jusqu'a 0 a udcmin. Tirer fort pres de la tension minimale fait encore chuter la tension : les
 * cellules passeraient sous leur minimum, et le BMS ouvrirait le SDC.
 *
 * @param finalSpnt consigne en %, reduite sur place si elle est positive
 * @param udc tension du bus continu, en V
 */
void UdcLimitCommand(float *finalSpnt, float udc)
{
    if (*finalSpnt <= 0.0f) // seule une demande de couple positif est limitee
    {
        return;
    }
    if (udc <= udcmin)
    {
        *finalSpnt = 0.0f;
        return;
    }
    if (udc < udcmin + udcDerateMarginV)
    {
        float maxAllowed = 100.0f * (udc - udcmin) / udcDerateMarginV;
        *finalSpnt = MIN(*finalSpnt, maxAllowed);
    }
}

bool TemperatureDerate(int temp, int tempMax, float *finalSpnt)
{
    float limit = 0;

    if (temp <= tempMax)
    {
        limit = 100.0f;
    }
    else if (temp < (tempMax + 2))
    {
        limit = 50.0f;
    }

    if (*finalSpnt >= 0)
        *finalSpnt = MIN(*finalSpnt, limit);
    else
        *finalSpnt = MAX(*finalSpnt, -limit);

    return limit < 100.0f;
}

/**
 * @brief Rampe de montee de la consigne, sur le temps reellement ecoule (R1) : la duree d'une montee ne
 * depend plus du nombre d'appels. La descente est immediate. Jamais de consigne negative (D3).
 *
 * @param potnom Demande en %, bornee a [throtmin, throtmax].
 * @param rampPerS Pente de la montee, en %/s.
 * @param elapsedMs Temps ecoule depuis l'appel precedent.
 * @return float Consigne apres la rampe, en %.
 */
float RampThrottle(float potnom, float rampPerS, uint32_t elapsedMs)
{
    // make sure potnom is within the boundaries of [throtmin, throtmax]
    potnom = MIN(potnom, throtmax);
    potnom = MAX(potnom, throtmin);

    if (potnom > throttleRamped)
    {
        float step = rampPerS * (float)elapsedMs / 1000.0f;
        throttleRamped = MIN(throttleRamped + step, potnom);
    }
    else
    {
        throttleRamped = potnom; // pas de rampe a la descente
    }

    return throttleRamped;
}
