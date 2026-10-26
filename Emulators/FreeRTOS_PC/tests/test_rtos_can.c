// Tests FreeRTOS du bus CAN (lot 4). Vrai noyau FreeRTOS, MainApp() du VCU, bus CAN simule.
//
// La tache de test a la priorite 18, TaskCanRx la priorite 6 : une serie d'envois depuis le test
// arrive en rafale, plus vite que TaskCanRx ne peut la lire, comme sur un vrai bus charge.

#include "emu.h"
#include "structs.h"
#include "canIds.h"
#include "queueHandles.h"
#include "throttle.h"
#include "vcu_test.h"

// R2 : quand la queue de reception deborde, c'est la valeur la plus recente qui doit survivre
static void TestCan_Burst(void)
{
    for (int32_t value = 1; value <= 50; value++)
    {
        (void)EmuCan_SendNow(CAN_ID_APPS1, value);
    }
    EmuWaitMs(20);

    int32_t last = 0;
    bool seen = EmuTrace_LastValue(xTRVPQueue, apps1Id, &last);
    vt_check("E-R2a", "T 11.9.2.d", seen && (last == 50),
             "rafale de 50 trames APPS1 (valeurs 1 a 50) -> derniere valeur transmise a TRVP : %ld (attendu 50)",
             seen ? (long)last : -1L);
}

// Chaine complete : capteurs et onduleur simules -> le VCU emet sa consigne vers l'onduleur
static void TestCan_TorqueFrame(void)
{
    // APPS a 40 % de la course, d'apres la calibration de initThrottleValues() (appelee par MainApp)
    int32_t apps1 = (int32_t)(potmin[0] + (potmax[0] - potmin[0]) * 0.4f);
    int32_t apps2 = (int32_t)(potmin[1] + (potmax[1] - potmin[1]) * 0.4f);
    EmuCan_SetPeriodic(CAN_ID_APPS1, 10U, apps1);
    EmuCan_SetPeriodic(CAN_ID_APPS2, 10U, apps2);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_FR, 10U, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_FL, 10U, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_RR, 10U, 0);
    EmuCan_SetPeriodic(CAN_ID_WHEEL_RL, 10U, 0);
    EmuDti_SetTemperatures(40, 40, 100U);
    EmuDti_SetVoltage(350, 100U);

    uint32_t start = EmuNowMs();
    int value = 0;
    bool positive = false;
    while (!positive && ((EmuNowMs() - start) < 500U))
    {
        EmuWaitMs(5);
        CanFrame_t frame = {0};
        if (EmuCan_LastTx(CAN_ID_DTI_SET_REL_CURRENT, &frame) && (frame.dlc >= 2U))
        {
            value = (int16_t)(((uint16_t)frame.data[0] << 8) | frame.data[1]);
            positive = (value > 0);
        }
    }
    uint32_t elapsed = EmuNowMs() - start;
    EmuCan_StopAll();

    vt_check("E-B2a", "-", positive,
             "APPS a 40 %%, temperatures, tension et roues periodiques -> consigne %d (0,1 %%) emise vers "
             "l'onduleur en %lu ms (attendu > 0 en moins de 500 ms)",
             value, (unsigned long)elapsed);
}

int EmuTest_Run(void)
{
    vt_begin("Emulateur FreeRTOS : bus CAN");

    TestCan_Burst();
    TestCan_TorqueFrame();

    return vt_end();
}
