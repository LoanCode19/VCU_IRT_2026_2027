// Test FreeRTOS d'exemple : verifie que l'emulateur lui-meme fonctionne.
// Vrai noyau FreeRTOS, MainApp() du VCU, bus CAN simule, temps reel.
//
// Modele pour les futurs tests FreeRTOS : inclure emu.h AVANT vcu_test.h, definir EmuTest_Run(),
// n'observer le VCU que de l'exterieur (trames CAN en entree, queues / sorties en sortie) pour que
// le test reste valable quand l'interieur de src/ change.

#include "emu.h"
#include "structs.h"
#include "canIds.h"
#include "queueHandles.h"
#include "vcu_test.h"

int EmuTest_Run(void)
{
    vt_begin("Emulateur FreeRTOS : demarrage");

    vt_check("RT-1", "-", xTaskGetSchedulerState() == taskSCHEDULER_RUNNING,
             "le scheduler FreeRTOS tourne apres MainApp()");

    uint32_t start = EmuNowMs();
    EmuWaitMs(200);
    uint32_t elapsed = EmuNowMs() - start;
    vt_check("RT-2", "-", (elapsed >= 200U) && (elapsed <= 210U), "vTaskDelay(200 ms) -> %lu ms ecoulees",
             (unsigned long)elapsed);

    // Messages comptes a l'envoi par l'espion des queues : que TaskRawValuesProcessing les lise
    // ou non (B8), le resultat est le meme
    uint32_t before = EmuTrace_SentCount(xTRVPQueue, apps1Id) + EmuTrace_SentCount(xTRVPQueue, apps2Id);
    (void)EmuCan_SendNow(CAN_ID_APPS1, 2000);
    (void)EmuCan_SendNow(CAN_ID_APPS2, 2100);
    EmuWaitMs(20);
    unsigned routed = (unsigned)(EmuTrace_SentCount(xTRVPQueue, apps1Id) + EmuTrace_SentCount(xTRVPQueue, apps2Id) - before);
    vt_check("RT-3", "-", routed == 2U, "2 trames APPS injectees -> %u message(s) routes vers TRVP par TaskCanRx",
             routed);

    // Capteur periodique : temperature moteur a 10 Hz pendant 350 ms -> 4 trames (t = 0, 100, 200, 300)
    before = EmuTrace_SentCount(xMainQueue, motor_tempId);
    EmuDti_SetTemperatures(45, 45, 100U);
    EmuWaitMs(350);
    EmuCan_StopAll();
    EmuWaitMs(20);
    unsigned temps = (unsigned)(EmuTrace_SentCount(xMainQueue, motor_tempId) - before);
    vt_check("RT-4", "-", (temps >= 3U) && (temps <= 5U),
             "capteur de temperature a 10 Hz pendant 350 ms -> %u message(s) vers Main (attendu 4)", temps);

    // Marge du tas (essai carte du lot 1b, emule : decision P10), avec le meme tas que la carte. Sur la
    // carte, l'autotest (mode 1) donne la valeur reelle : chaque tache y porte en plus sa structure newlib.
    size_t minFree = xPortGetMinimumEverFreeHeapSize();
    vt_check("E-R4b", "-", minFree >= (configTOTAL_HEAP_SIZE / 4U),
             "tas FreeRTOS : %u octets utilises au plus sur %u, %u libres au minimum (attendu au moins 25 %%, %u)",
             (unsigned)(configTOTAL_HEAP_SIZE - minFree), (unsigned)configTOTAL_HEAP_SIZE, (unsigned)minFree,
             (unsigned)(configTOTAL_HEAP_SIZE / 4U));

    return vt_end();
}
