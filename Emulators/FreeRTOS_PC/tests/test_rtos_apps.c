// Tests FreeRTOS de la pedale d'accelerateur (lot 2, S1) : toute la chaine (TaskCanRx, TRVP, Main,
// TaskCanTx) avec le vrai FreeRTOS. Le banc ne peut pas verifier le verrouillage de la faute : l'etat
// de TaskRawValuesProcessing est perdu entre deux passages simules.

#include "emu.h"
#include "structs.h"
#include "canIds.h"
#include "throttle.h"
#include "vcu_test.h"

// Valeur brute d'un APPS pour un pourcentage de sa course (calibration de initThrottleValues())
static int32_t AppsRaw(int potIdx, float percent)
{
    return (int32_t)(potmin[potIdx] + (potmax[potIdx] - potmin[potIdx]) * percent / 100.0f);
}

// Temps pour qu'une nouvelle valeur traverse la chaine : une periode de TRVP, une de Main, et une marge
#define CHAIN_DELAY_MS (2U * CONTROL_PERIOD_MS + 5U)

// Nouvelles valeurs des APPS, envoyees tout de suite puis a chaque trame periodique
static void SetApps(float percent1, float percent2)
{
    EmuCan_SetValue(CAN_ID_APPS1, AppsRaw(0, percent1));
    EmuCan_SetValue(CAN_ID_APPS2, AppsRaw(1, percent2));
    (void)EmuCan_SendNow(CAN_ID_APPS1, AppsRaw(0, percent1));
    (void)EmuCan_SendNow(CAN_ID_APPS2, AppsRaw(1, percent2));
}

// Capteurs, roues et onduleur simules, APPS coherents a 40 %
static void StartCar(void)
{
    EmuCan_SetPeriodic(CAN_ID_APPS1, 10U, AppsRaw(0, 40.0f));
    EmuCan_SetPeriodic(CAN_ID_APPS2, 10U, AppsRaw(1, 40.0f));
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

// Incoherence courte (50 ms) : couple coupe tout de suite, sans verrouillage. APPS de nouveau
// coherents -> le couple revient.
static void TestApps_ShortImplausibility(void)
{
    SetApps(30.0f, 70.0f);
    EmuWaitMs(CHAIN_DELAY_MS); // le temps que les nouvelles valeurs traversent la chaine
    int during = MaxTorqueDuring(50U - CHAIN_DELAY_MS);
    SetApps(40.0f, 40.0f);
    unsigned long back = (unsigned long)TimeToTorque(300U);
    vt_check("E-S1f", "T 11.8.8", (during <= 0) && (back <= 300UL),
             "APPS 30 %% / 70 %% pendant 50 ms -> consigne max %d (0,1 %%) pendant l'incoherence ; APPS de nouveau "
             "coherents -> couple revenu en %lu ms (attendu 0, puis retour en moins de 300 ms : faute non confirmee)",
             during, back);
}

// Incoherence de plus de 100 ms : faute confirmee. Le couple reste coupe quand les APPS redeviennent
// coherents, jusqu'a ce que la pedale repasse sous 5 % (D2).
static void TestApps_LatchedImplausibility(void)
{
    SetApps(30.0f, 70.0f);
    EmuWaitMs(200);
    SetApps(40.0f, 40.0f);
    EmuWaitMs(20);
    int latched = MaxTorqueDuring(300U);
    SetApps(0.0f, 0.0f);
    EmuWaitMs(100);
    SetApps(40.0f, 40.0f);
    unsigned long back = (unsigned long)TimeToTorque(300U);
    vt_check("E-S1g", "T 11.8.8", (latched <= 0) && (back <= 300UL),
             "APPS 30 %% / 70 %% pendant 200 ms, puis coherents a 40 %% -> consigne max %d (0,1 %%) pendant 300 ms ; "
             "pedale relachee puis 40 %% -> couple revenu en %lu ms (attendu 0 : faute confirmee ; puis retour "
             "apres la pedale relachee, D2)",
             latched, back);
}

int EmuTest_Run(void)
{
    vt_begin("Emulateur FreeRTOS : pedale (APPS)");
    StartCar();
    unsigned long first = (unsigned long)TimeToTorque(500U);
    if (first > 500UL)
    {
        vt_blocked("E-S1f", "T 11.8.8", "APPS coherents a 40 %% : aucune consigne > 0, verrouillage non verifiable");
        vt_blocked("E-S1g", "T 11.8.8", "APPS coherents a 40 %% : aucune consigne > 0, verrouillage non verifiable");
    }
    else
    {
        TestApps_ShortImplausibility();
        TestApps_LatchedImplausibility();
    }
    EmuCan_StopAll();
    return vt_end();
}
