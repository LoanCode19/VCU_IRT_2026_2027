// Tests FreeRTOS du bus CAN (lot 4). Vrai noyau FreeRTOS, MainApp() du VCU, bus CAN simule.
//
// La tache de test a la priorite 18, TaskCanRx la priorite 6 : une serie d'envois depuis le test
// arrive en rafale, plus vite que TaskCanRx ne peut la lire, comme sur un vrai bus charge.

#include "emu.h"
#include "structs.h"
#include "canIds.h"
#include "queueHandles.h"
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

int EmuTest_Run(void)
{
    vt_begin("Emulateur FreeRTOS : bus CAN");

    TestCan_Burst();

    return vt_end();
}
