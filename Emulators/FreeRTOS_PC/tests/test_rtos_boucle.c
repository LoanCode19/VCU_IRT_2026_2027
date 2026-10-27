// Tests FreeRTOS de la boucle de controle (lot 5, R1) : la consigne doit partir a chaque periode et la
// rampe suivre le temps, quel que soit le trafic CAN. Ils ont besoin du vrai temps reel.

#include "emu.h"
#include "structs.h"
#include "canIds.h"
#include "throttle.h"
#include "vcu_test.h"

#define MAX_GAP_MS 20U            // DTI : au moins deux consignes par delai de coupure de 100 ms (D6)
// Pedale a 35 % : sous le plafond de 40 % du limiteur actuel (B3), seul R1 est mesure. La zone morte de 2 %
// ramene la demande a 33,7 % : la rampe est chronometree jusqu'a 30 % de consigne.
#define RAMP_PEDAL_PERCENT 35.0f
#define RAMP_TARGET_PERCENT 30.0f
#define RAMP_TIMEOUT_MS 1000U

// Pente de la rampe de montee a basse vitesse, en %/s
static float RampPerS(void)
{
    return throtRampPerS;
}

// Valeur brute d'un APPS pour un pourcentage de sa course (calibration de initThrottleValues())
static int32_t AppsRaw(int potIdx, float percent)
{
    return (int32_t)(potmin[potIdx] + (potmax[potIdx] - potmin[potIdx]) * percent / 100.0f);
}

// APPS et roues (a l'arret) toutes les sensorMs, onduleur toutes les inverterMs
static void SetTraffic(float appsPercent, uint32_t sensorMs, uint32_t inverterMs)
{
    EmuCan_SetPeriodic(CAN_ID_APPS1, sensorMs, AppsRaw(0, appsPercent));
    EmuCan_SetPeriodic(CAN_ID_APPS2, sensorMs, AppsRaw(1, appsPercent));
    EmuCan_SetPeriodic(CAN_ID_WHEEL_FR, sensorMs, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_FL, sensorMs, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_RR, sensorMs, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_RL, sensorMs, 0);
    EmuDti_SetTemperatures(40, 40, inverterMs);
    EmuDti_SetVoltage(350, inverterMs);
}

// Derniere consigne envoyee a l'onduleur, en 0,1 % ; -1 si aucune
static int LastTorque(void)
{
    CanFrame_t frame = {0};
    if (!EmuCan_LastTx(CAN_ID_DTI_SET_REL_CURRENT, &frame) || (frame.dlc < 2U))
    {
        return -1;
    }
    return (int16_t)(((uint16_t)frame.data[0] << 8) | frame.data[1]);
}

// Plus long ecart entre deux consignes pendant ms, fin de la fenetre comprise, lu toutes les millisecondes
static uint32_t MaxGapDuring(uint32_t ms, uint32_t *count)
{
    uint32_t seen = EmuCan_TxCount(CAN_ID_DTI_SET_REL_CURRENT);
    uint32_t start = EmuNowMs();
    uint32_t last = start;
    uint32_t maxGap = 0;
    *count = 0;
    while ((EmuNowMs() - start) < ms)
    {
        EmuWaitMs(1);
        uint32_t sent = EmuCan_TxCount(CAN_ID_DTI_SET_REL_CURRENT);
        if (sent != seen)
        {
            uint32_t now = EmuNowMs();
            *count += sent - seen;
            seen = sent;
            maxGap = ((now - last) > maxGap) ? (now - last) : maxGap;
            last = now;
        }
    }
    uint32_t end = EmuNowMs();
    return ((end - last) > maxGap) ? (end - last) : maxGap;
}

// Temps pour que la consigne passe de 0 a RAMP_TARGET_PERCENT apres un appui franc sur la pedale (a
// RAMP_PEDAL_PERCENT), avec ce trafic ; UINT32_MAX si elle n'y arrive pas en RAMP_TIMEOUT_MS
static uint32_t RampDuration(uint32_t sensorMs, uint32_t inverterMs)
{
    SetTraffic(0.0f, sensorMs, inverterMs);
    EmuWaitMs(300); // pedale relachee : la rampe repart de 0
    // Les deux APPS changent tout de suite, sans attendre leur prochaine trame periodique : la mesure ne
    // depend pas de la phase des capteurs
    EmuCan_SetValue(CAN_ID_APPS1, AppsRaw(0, RAMP_PEDAL_PERCENT));
    EmuCan_SetValue(CAN_ID_APPS2, AppsRaw(1, RAMP_PEDAL_PERCENT));
    (void)EmuCan_SendNow(CAN_ID_APPS1, AppsRaw(0, RAMP_PEDAL_PERCENT));
    (void)EmuCan_SendNow(CAN_ID_APPS2, AppsRaw(1, RAMP_PEDAL_PERCENT));
    uint32_t start = EmuNowMs();
    int target = (int)(RAMP_TARGET_PERCENT * 10.0f); // en 0,1 %
    while ((EmuNowMs() - start) < RAMP_TIMEOUT_MS)
    {
        EmuWaitMs(1);
        if (LastTorque() >= target)
        {
            return EmuNowMs() - start;
        }
    }
    return UINT32_MAX;
}

// En roulage, avec des capteurs lents : l'onduleur doit quand meme recevoir une consigne a chaque periode
static void TestBoucle_PeriodicCommand(void)
{
    SetTraffic(20.0f, 50U, 100U);
    EmuWaitMs(300);
    uint32_t count = 0;
    uint32_t maxGap = MaxGapDuring(1000U, &count);
    vt_check("E-R1a", "-", maxGap <= MAX_GAP_MS,
             "capteurs toutes les 50 ms, onduleur toutes les 100 ms, pendant 1 s -> %lu consigne(s), plus long ecart "
             "%lu ms (attendu au plus %u ms ; le DTI coupe apres 100 ms sans consigne)",
             (unsigned long)count, (unsigned long)maxGap, MAX_GAP_MS);
}

// La duree d'une rampe ne doit pas dependre du nombre de messages recus
static void TestBoucle_RampVsTraffic(void)
{
    uint32_t low = RampDuration(50U, 100U);
    uint32_t high = RampDuration(5U, 10U);
    float expected = RAMP_TARGET_PERCENT / RampPerS() * 1000.0f; // ms
    bool reached = (low != UINT32_MAX) && (high != UINT32_MAX);
    long diff = (long)low - (long)high;
    bool sameDuration = reached && (diff <= (long)MAX_GAP_MS) && (diff >= -(long)MAX_GAP_MS);
    bool nearExpected = reached && ((float)low >= expected - 10.0f) && ((float)low <= expected + 40.0f) &&
                        ((float)high >= expected - 10.0f) && ((float)high <= expected + 40.0f);
    vt_check("E-R1b", "-", sameDuration && nearExpected,
             "pedale a %.0f %%, consigne de 0 a %.0f %% : %ld ms avec un trafic faible (capteurs 50 ms, onduleur "
             "100 ms), %ld ms avec un trafic fort (5 ms, 10 ms) (attendu la meme duree a %u ms pres, entre %.0f et "
             "%.0f ms : %.0f %%/s)",
             RAMP_PEDAL_PERCENT, RAMP_TARGET_PERCENT, (low == UINT32_MAX) ? -1L : (long)low,
             (high == UINT32_MAX) ? -1L : (long)high,
             MAX_GAP_MS, expected - 10.0f, expected + 40.0f, RampPerS());
}

int EmuTest_Run(void)
{
    vt_begin("Emulateur FreeRTOS : boucle de controle");

    TestBoucle_PeriodicCommand();
    TestBoucle_RampVsTraffic();

    EmuCan_StopAll();
    return vt_end();
}
