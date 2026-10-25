#ifndef TEST_HOST_TEST_APP_H
#define TEST_HOST_TEST_APP_H

// Outils communs aux tests qui font tourner les vraies taches du VCU (sans ordonnanceur).

#include <setjmp.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "test_rtos.h"
#include "VCU.h"

// Execute une tache jusqu'a son premier blocage (queue vide avec attente, vTaskDelayUntil...).
static void RunTaskOnce(TaskFunction_t task, void *params)
{
    jmp_buf env;
    vTestEnableEscapeWhenQueueEmpty(&env);
    if (setjmp(env) == 0)
    {
        task(params);
    }
    vTestDisableEscapeWhenQueueEmpty();
}

// Execute MainApp() comme sur la carte : les queues sont creees avec les memes tailles,
// les xTaskCreate() sont enregistres (test_rtos.h), puis vTaskStartScheduler() rend la main.
static void BootMainApp(void)
{
    jmp_buf env;
    vTestResetCreatedTasks();
    vTestEnableEscapeWhenQueueEmpty(&env);
    if (setjmp(env) == 0)
    {
        MainApp();
    }
    vTestDisableEscapeWhenQueueEmpty();
}

#endif // TEST_HOST_TEST_APP_H
