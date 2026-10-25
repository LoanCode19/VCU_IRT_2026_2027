#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "console.h"
#include "VCU.h"
#include "emu.h"

// Point d'entree de l'emulateur PC : remplace main() de VCU_STM32F407/Core/Src/main.c.
//
// Comme sur la carte, MainApp() cree les queues et les taches du VCU, puis le scheduler demarre
// (osKernelStart() dans main.c sur la carte). Avant, on cree la tache du bus CAN simule et la
// tache qui execute le test (EmuTest_Run).

#define EMU_TEST_STACK_DEPTH 1024U

static StaticTask_t sTestTaskTcb;
static StackType_t sTestTaskStack[EMU_TEST_STACK_DEPTH];

static StaticTask_t sIdleTaskTcb;
static StackType_t sIdleTaskStack[configMINIMAL_STACK_SIZE];

// Arret immediat : vTaskEndScheduler() n'existe pas avec le port Windows
static void EmuExit(int code)
{
    fflush(stdout);
    fflush(stderr);
    _Exit(code);
}

static void EmuTestTask(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(10)); // laisse les taches du VCU demarrer
    EmuExit(EmuTest_Run());
}

int main(void)
{
    console_init();
    EmuCan_Init();
    (void)xTaskCreateStatic(EmuTestTask, "EMU_TEST", EMU_TEST_STACK_DEPTH, NULL,
                            configMAX_PRIORITIES - 2, sTestTaskStack, &sTestTaskTcb);

    MainApp();
    vTaskStartScheduler(); // ne rend jamais la main
    return 0;
}

// ----------------------------------------------------------------------------------------------
// Fonctions exigees par FreeRTOSConfig.h

void vAssertCalled(const char *file, unsigned long line)
{
    fprintf(stderr, "ASSERT FreeRTOS : %s:%lu\n", file, line);
    EmuExit(3);
}

void vApplicationMallocFailedHook(void)
{
    fprintf(stderr, "Tas FreeRTOS epuise (configTOTAL_HEAP_SIZE = %u octets, comme sur la carte)\n",
            (unsigned)configTOTAL_HEAP_SIZE);
    EmuExit(4);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *stackSize)
{
    *tcb = &sIdleTaskTcb;
    *stack = sIdleTaskStack;
    *stackSize = configMINIMAL_STACK_SIZE;
}
