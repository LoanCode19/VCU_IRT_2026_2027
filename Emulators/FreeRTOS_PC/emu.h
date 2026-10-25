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
// Les trames passent par EmuCan_DeliverFrame(), copie de l'ISR de reception de la carte
// (HAL_CAN_RxFifo0MsgPendingCallback dans VCU_STM32F407/Core/Src/main.c) : meme conversion,
// meme queue. A garder synchronisee avec l'ISR quand celle-ci change.

#define EMU_CAN_MAX_SIGNALS 16U

// Envoie une trame tout de suite. La valeur est codee sur les 4 premiers octets, big-endian.
bool EmuCan_SendNow(uint32_t stdId, int32_t value);

// Capteur simule : emet stdId toutes les periodMs avec la derniere valeur fixee.
void EmuCan_SetPeriodic(uint32_t stdId, uint32_t periodMs, int32_t value);
void EmuCan_SetValue(uint32_t stdId, int32_t value);
void EmuCan_Mute(uint32_t stdId, bool muted); // panne : le capteur se tait (fil coupe, ECU en defaut)
void EmuCan_StopAll(void);

// Trames que l'ISR n'a pas pu deposer (queue de reception pleine) depuis le debut du test.
uint32_t EmuCan_DroppedFrames(void);

// Appele par main_pc.c avant MainApp() : cree la tache "bus CAN" (1 ms, priorite max).
void EmuCan_Init(void);

// ----------------------------------------------------------------------------------------------
// Espion des queues (emu_trace.c)
//
// Nombre de dataMessage_t d'identifiant messageId deposes dans queue par les taches (xQueueSend)
// depuis le demarrage. La tache qui lit la queue n'y change rien : on observe l'envoi, pas le stock.
uint32_t EmuTrace_SentCount(QueueHandle_t queue, int messageId);

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
