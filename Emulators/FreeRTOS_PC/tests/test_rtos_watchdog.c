// Tests FreeRTOS du watchdog (lot 3) : les essais de la carte, emules faute d'electronique (decision P10).
// TaskWDG rafraichit l'IWDG (modele dans main_pc.c) seulement si toutes les taches surveillees sont
// a l'heure. Il faut USE_WDG, defini par run_emulator.py comme sur la carte.

#include "emu.h"
#include "structs.h"
#include "canIds.h"
#include "project_config.h"
#include "throttle.h"
#include "vcu_test.h"

#ifdef USE_WDG

// Capteurs et onduleur simules, comme dans E-B2a : toute la chaine tourne
static void StartSensors(void)
{
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
}

// Marche normale : sans aucune trame (comme la carte nue), puis avec toute la chaine, l'IWDG doit
// etre rafraichi assez souvent pour son delai le plus court. W6 : avec le trafic CAN, la queue du
// watchdog (10 messages) deborde, des signes de vie sont perdus et une tache lente parait en retard.
static void TestWdg_NormalRun(void)
{
    uint32_t before = EmuIwdg_RefreshCount();
    EmuWaitMs(1000);
    StartSensors();
    EmuWaitMs(1000);
    EmuCan_StopAll();
    unsigned long refreshes = (unsigned long)(EmuIwdg_RefreshCount() - before);
    unsigned long sinceLast = (unsigned long)(EmuNowMs() - EmuIwdg_LastRefreshMs());
    unsigned long maxGap = (unsigned long)EmuIwdg_MaxGapMs();
    if (sinceLast > maxGap)
    {
        maxGap = sinceLast; // l'ecart en cours compte aussi
    }
    vt_check("E-W6a", "-", (refreshes >= 15UL) && (maxGap < EMU_IWDG_MIN_TIMEOUT_MS),
             "marche normale 2 s (sans trame, puis avec capteurs et onduleur) -> %lu rafraichissements de l'IWDG, "
             "plus long ecart %lu ms depuis le demarrage (attendu environ 20, ecart < %u ms : delai le plus court "
             "de l'IWDG)",
             refreshes, maxGap, EMU_IWDG_MIN_TIMEOUT_MS);
}

// Tache bloquee : TaskIHM suspendue. TaskWDG doit cesser de rafraichir l'IWDG apres le delai de
// TaskIHM, puis ne plus jamais le rafraichir : la carte redemarre, quel que soit le delai reel de
// l'IWDG. A lancer en dernier : le VCU reste en panne.
static void TestWdg_BlockedTask(void)
{
    TaskHandle_t ihm = xTaskGetHandle("IHM");
    if (ihm == NULL)
    {
        vt_blocked("E-W1b", "-", "tache IHM introuvable (nom change dans MainApp() ?)");
        return;
    }
    const uint32_t ihmTimeoutMs = (uint32_t)IHM_WDG_TIMEOUT * portTICK_PERIOD_MS;
    uint32_t suspendedAt = EmuNowMs();
    vTaskSuspend(ihm);
    EmuWaitMs(ihmTimeoutMs + 100U + EMU_IWDG_MAX_TIMEOUT_MS + 200U);

    uint32_t last = EmuIwdg_LastRefreshMs();
    unsigned long stoppedAfter = (unsigned long)((last > suspendedAt) ? (last - suspendedAt) : 0U);
    unsigned long silence = (unsigned long)(EmuNowMs() - last);
    unsigned long limit = (unsigned long)(ihmTimeoutMs + 100U + 20U); // delai de TaskIHM + periode de TaskWDG + gigue
    vt_check("E-W1b", "-", (stoppedAfter <= limit) && (silence > EMU_IWDG_MAX_TIMEOUT_MS),
             "TaskIHM suspendue -> dernier rafraichissement %lu ms apres, puis plus rien pendant %lu ms : la carte "
             "redemarre entre %lu et %lu ms apres la panne (attendu : arret en moins de %lu ms, silence > %u ms)",
             stoppedAfter, silence, stoppedAfter + EMU_IWDG_MIN_TIMEOUT_MS, stoppedAfter + EMU_IWDG_MAX_TIMEOUT_MS,
             limit, EMU_IWDG_MAX_TIMEOUT_MS);
}

#endif // USE_WDG

int EmuTest_Run(void)
{
    vt_begin("Emulateur FreeRTOS : watchdog");
#ifdef USE_WDG
    TestWdg_NormalRun();
    TestWdg_BlockedTask();
#else
    vt_blocked("E-W6a", "-", "TaskWDG absent : compiler avec USE_WDG, comme la carte");
    vt_blocked("E-W1b", "-", "TaskWDG absent : compiler avec USE_WDG, comme la carte");
#endif
    return vt_end();
}
