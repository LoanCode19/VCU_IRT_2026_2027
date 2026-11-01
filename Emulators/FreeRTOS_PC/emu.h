#ifndef EMU_H
#define EMU_H

// Emulateur PC du VCU : vrai noyau FreeRTOS (port Windows) + bus CAN simule.
//
// Un test FreeRTOS est un fichier tests/test_*.c qui definit EmuTest_Run(). main_pc.c le lance
// dans une tache prioritaire, apres que MainApp() a cree les queues et les taches du VCU,
// exactement comme sur la carte. Le temps est le vrai temps (1 tick = 1 ms).

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"

#ifdef VCU_TEST_H
#error "Inclure emu.h avant vcu_test.h (les resultats doivent passer par le mutex de la console)"
#endif

// Affichage protege par le mutex de la console (console_pc.c). vcu_test.h l'utilise via VT_VPRINTF.
int emu_vprintf(const char *fmt, va_list args);
#define VT_VPRINTF emu_vprintf

// A definir par chaque test. Renvoie vt_end() (0 si aucun FAIL).
int EmuTest_Run(void);

// ----------------------------------------------------------------------------------------------
// Bus CAN simule (emu_can.c)
//
// Les trames sont remises a CanRx_OnFrameFromISR(), le point d'entree que l'ISR de reception
// de la carte appelle aussi (HAL_CAN_RxFifo0MsgPendingCallback dans VCU_STM32F407/Core/Src/main.c).

#define EMU_CAN_MAX_SIGNALS 16U

// Envoie une trame quelconque tout de suite (dlc octets utiles dans data).
bool EmuCan_SendFrame(uint32_t stdId, uint8_t dlc, const uint8_t data[8]);

// Envoie une trame tout de suite au format provisoire des capteurs : la valeur sur les 4 premiers
// octets, big-endian, DLC 4.
bool EmuCan_SendNow(uint32_t stdId, int32_t value);

// Capteur simule : emet stdId toutes les periodMs avec le dernier contenu fixe.
void EmuCan_SetPeriodicFrame(uint32_t stdId, uint32_t periodMs, uint8_t dlc, const uint8_t data[8]);
void EmuCan_SetPeriodic(uint32_t stdId, uint32_t periodMs, int32_t value); // format provisoire
void EmuCan_SetValue(uint32_t stdId, int32_t value);
void EmuCan_Mute(uint32_t stdId, bool muted); // panne : le capteur se tait (fil coupe, ECU en defaut)
void EmuCan_StopAll(void);

// Trames arrivees alors que la queue de reception etait pleine, depuis le debut du test.
uint32_t EmuCan_DroppedFrames(void);

// Appele par main_pc.c avant MainApp() : cree la tache "bus CAN" (1 ms, priorite max).
void EmuCan_Init(void);

// Trames emises par le VCU (CanTx_Send) depuis le demarrage : nombre, et derniere trame de cet ID
uint32_t EmuCan_TxCount(uint32_t stdId);
bool EmuCan_LastTx(uint32_t stdId, CanFrame_t *frame);

// Etat du vehicule (trame provisoire 0x012, T19) : TS actif, SDC ferme, frein, interrupteur R2D, un octet chacun
// (0 ou 1), toutes les periodMs.
#define EMU_VEHICLE_PERIOD_MS 10U
void EmuVehicle_SetState(bool tsActive, bool sdcClosed, bool brake, bool r2dSwitch, uint32_t periodMs);
// Sequence Ready-to-Drive par le bus (S3, T8) : TS actif et interrupteur sur OFF, puis sur ON pendant un
// freinage, puis frein relache. A faire pedale relachee : frein et pedale ensemble verrouillent le couple (T10).
void EmuVehicle_EnterR2D(void);

// Onduleur simule : ses trames periodiques, dans le format que le VCU attend
void EmuDti_SetTemperatures(int32_t motorC, int32_t inverterC, uint32_t periodMs);
void EmuDti_SetVoltage(int32_t volts, uint32_t periodMs);

// ----------------------------------------------------------------------------------------------
// Espion des queues (emu_trace.c)
//
// Nombre de dataMessage_t d'identifiant messageId deposes dans queue par les taches (xQueueSend)
// depuis le demarrage. La tache qui lit la queue n'y change rien : on observe l'envoi, pas le stock.
uint32_t EmuTrace_SentCount(QueueHandle_t queue, int messageId);

// data du dernier dataMessage_t d'identifiant messageId depose dans queue. false si aucun.
bool EmuTrace_LastValue(QueueHandle_t queue, int messageId, int32_t *value);

// ----------------------------------------------------------------------------------------------
// Watchdog (main_pc.c) : modele de l'IWDG de la carte, rafraichi par TaskWDG (USE_WDG).
// Delai de l'IWDG de la carte : (500 + 1) x 32 / f_LSI, avec f_LSI de 17 a 47 kHz selon la puce.
#define EMU_IWDG_MIN_TIMEOUT_MS 340U // 341 ms a 47 kHz : le redemarrage le plus rapide
#define EMU_IWDG_MAX_TIMEOUT_MS 945U // 943 ms a 17 kHz : le redemarrage le plus lent

uint32_t EmuIwdg_RefreshCount(void);  // appels a refreshIWDG() depuis le demarrage
uint32_t EmuIwdg_LastRefreshMs(void); // heure du dernier appel (0 : jamais)
uint32_t EmuIwdg_MaxGapMs(void);      // plus long ecart entre deux appels, demarrage compris

// ----------------------------------------------------------------------------------------------
// Outils de test

static inline void EmuWaitMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline uint32_t EmuNowMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

#endif // EMU_H
