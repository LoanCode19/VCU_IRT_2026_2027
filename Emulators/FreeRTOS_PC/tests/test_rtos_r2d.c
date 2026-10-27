// Tests FreeRTOS du Ready-to-Drive (lot 6, S3) : la sequence complete par le bus, avec le vrai FreeRTOS et toute la
// chaine (TaskCanRx, TRVP, Main, TaskCanTx). L'etat du vehicule arrive dans la trame provisoire 0x012 (T19).

#include "emu.h"
#include "structs.h"
#include "canIds.h"
#include "throttle.h"
#include "vcu_test.h"

#define SDC_EXIT_MAX_MS 30U // EV 4.11.8 : sortie immediate ; trois periodes de la boucle au plus

// Valeur brute d'un APPS pour un pourcentage de sa course (calibration de initThrottleValues())
static int32_t AppsRaw(int potIdx, float percent)
{
    return (int32_t)(potmin[potIdx] + (potmax[potIdx] - potmin[potIdx]) * percent / 100.0f);
}

// Nouvelle position de la pedale, envoyee tout de suite puis a chaque trame periodique
static void SetApps(float percent)
{
    EmuCan_SetValue(CAN_ID_APPS1, AppsRaw(0, percent));
    EmuCan_SetValue(CAN_ID_APPS2, AppsRaw(1, percent));
    (void)EmuCan_SendNow(CAN_ID_APPS1, AppsRaw(0, percent));
    (void)EmuCan_SendNow(CAN_ID_APPS2, AppsRaw(1, percent));
}

// Capteurs, roues et onduleur simules, pedale relachee
static void StartSensors(void)
{
    EmuCan_SetPeriodic(CAN_ID_APPS1, 10U, AppsRaw(0, 0.0f));
    EmuCan_SetPeriodic(CAN_ID_APPS2, 10U, AppsRaw(1, 0.0f));
    EmuCan_SetPeriodic(CAN_ID_WHEEL_FR, 10U, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_FL, 10U, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_RR, 10U, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_RL, 10U, 0);
    EmuDti_SetTemperatures(40, 40, 100U);
    EmuDti_SetVoltage(350, 100U);
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

// Derniere commande "Drive enable" envoyee a l'onduleur (T20) : 0, 1, ou -1 si aucune
static int LastDriveEnable(void)
{
    CanFrame_t frame = {0};
    if (!EmuCan_LastTx(CAN_ID_DTI_DRIVE_ENABLE, &frame) || (frame.dlc < 1U))
    {
        return -1;
    }
    return frame.data[0];
}

// Plus grande consigne emise pendant ms, lue toutes les millisecondes ; -1 si aucune
static int MaxTorqueDuring(uint32_t ms)
{
    int maxTorque = -1;
    uint32_t seen = EmuCan_TxCount(CAN_ID_DTI_SET_REL_CURRENT);
    for (uint32_t t = 0; t < ms; t++)
    {
        EmuWaitMs(1);
        uint32_t count = EmuCan_TxCount(CAN_ID_DTI_SET_REL_CURRENT);
        if (count != seen)
        {
            seen = count;
            int torque = LastTorque();
            maxTorque = (torque > maxTorque) ? torque : maxTorque;
        }
    }
    return maxTorque;
}

// Temps avant une consigne > 0, en ms ; ms + 1 si aucune pendant ms
static uint32_t TimeToTorque(uint32_t ms)
{
    uint32_t seen = EmuCan_TxCount(CAN_ID_DTI_SET_REL_CURRENT);
    for (uint32_t t = 1; t <= ms; t++)
    {
        EmuWaitMs(1);
        uint32_t count = EmuCan_TxCount(CAN_ID_DTI_SET_REL_CURRENT);
        if ((count != seen) && (LastTorque() > 0))
        {
            return t;
        }
        seen = count;
    }
    return ms + 1U;
}

// TS actif sans sequence : pas de couple. Interrupteur sur ON pendant un freinage, frein relache, pedale : couple
// et "Drive enable" a 1.
static void TestR2D_Sequence(void)
{
    StartSensors();
    EmuVehicle_SetState(true, true, false, false, EMU_VEHICLE_PERIOD_MS); // TS actif, interrupteur sur OFF
    EmuWaitMs(100);
    SetApps(40.0f);
    int withoutR2D = MaxTorqueDuring(200U);

    SetApps(0.0f); // pedale relachee pendant le freinage (T10)
    EmuWaitMs(50);
    EmuVehicle_SetState(true, true, true, true, EMU_VEHICLE_PERIOD_MS); // interrupteur sur ON, frein appuye
    EmuWaitMs(30);
    EmuVehicle_SetState(true, true, false, true, EMU_VEHICLE_PERIOD_MS); // frein relache
    EmuWaitMs(30);
    SetApps(40.0f);
    unsigned long toTorque = (unsigned long)TimeToTorque(500U);
    int enable = LastDriveEnable();

    vt_check("E-S3c", "EV 4.11.7", (withoutR2D <= 0) && (toTorque <= 500UL) && (enable == 1),
             "TS actif, pedale 40 %% sans sequence -> consigne max %d (0,1 %%) ; interrupteur sur ON pendant un "
             "freinage, puis pedale 40 %% -> couple en %lu ms, Drive enable %d (attendu 0, puis du couple en moins de "
             "500 ms et Drive enable a 1)",
             withoutR2D, toTorque, enable);
}

// En R2D avec du couple, le SDC s'ouvre : consigne 0 et "Drive enable" a 0 tout de suite
static void TestR2D_SdcOpens(void)
{
    if (LastTorque() <= 0)
    {
        vt_blocked("E-S3d", "EV 4.11.8", "pas de couple en R2D (E-S3c) : sortie a l'ouverture du SDC non verifiable");
        return;
    }

    uint32_t before = EmuCan_TxCount(CAN_ID_DTI_SET_REL_CURRENT);
    uint32_t start = EmuNowMs();
    EmuVehicle_SetState(true, false, false, true, EMU_VEHICLE_PERIOD_MS); // SDC ouvert
    uint32_t elapsed = UINT32_MAX;
    while ((elapsed == UINT32_MAX) && ((EmuNowMs() - start) < 100U))
    {
        EmuWaitMs(1);
        if ((EmuCan_TxCount(CAN_ID_DTI_SET_REL_CURRENT) != before) && (LastTorque() == 0) && (LastDriveEnable() == 0))
        {
            elapsed = EmuNowMs() - start;
        }
    }

    vt_check("E-S3d", "EV 4.11.8", elapsed <= SDC_EXIT_MAX_MS,
             "en R2D avec du couple, le SDC s'ouvre -> consigne 0 et Drive enable a 0 en %ld ms (attendu au plus "
             "%u ms ; -1 : jamais en 100 ms)",
             (elapsed == UINT32_MAX) ? -1L : (long)elapsed, SDC_EXIT_MAX_MS);
}

int EmuTest_Run(void)
{
    vt_begin("Emulateur FreeRTOS : Ready-to-Drive");

    TestR2D_Sequence();
    TestR2D_SdcOpens();

    EmuCan_StopAll();
    return vt_end();
}
