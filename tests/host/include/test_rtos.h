#ifndef TEST_HOST_TEST_RTOS_H
#define TEST_HOST_TEST_RTOS_H

// Commandes du faux FreeRTOS (freertos_stubs.c) reservees aux tests.
//
// Principe : on n'emule pas l'ordonnanceur. Une tache est appelee directement par le test
// et tourne jusqu'a ce qu'elle se bloque (queue vide avec attente, vTaskDelayUntil...).
// A ce moment, le stub fait un longjmp vers le test : la tache a execute "un passage".

#include <stdbool.h>
#include <setjmp.h>
#include "FreeRTOS.h"
#include "task.h"

// true (defaut, comportement historique) : chaque xTaskGetTickCount() avance d'un tick.
// false : le temps ne bouge que via vTestSetTickCount() ou les attentes simulees.
void vTestSetTickAutoIncrement(bool enable);

// Nombre d'attentes avec timeout fini (queue vide) simulees avant de sortir de la tache.
// Chaque attente simulee fait avancer le temps du timeout puis renvoie pdFAIL.
// 0 (defaut) : sortie des le premier blocage. Une attente portMAX_DELAY sort toujours.
void vTestSetBlockingTimeoutsBeforeEscape(unsigned count);

// vTaskDelayUntil / vTaskDelay : nombre de reveils autorises avant de sortir de la tache,
// et fonction appelee a chaque reveil (pour injecter des messages au bon moment).
typedef void (*TestWakeHook_t)(TickType_t now);
void vTestSetDelayCallsBeforeEscape(unsigned count, TestWakeHook_t hook);

// Taches creees via xTaskCreate() depuis le dernier vTestResetCreatedTasks().
typedef struct
{
    TaskFunction_t function;
    const char *name;
    void *parameters;
    UBaseType_t priority;
    bool parametersOnStack; // pvParameters pointe dans la pile de l'appelant (probleme B5)
} TestCreatedTask_t;

void vTestResetCreatedTasks(void);
unsigned uxTestCreatedTaskCount(void);
const TestCreatedTask_t *pxTestCreatedTask(unsigned index);

// Adresse d'une variable locale de main() : sert de borne haute de la pile pour detecter B5.
void vTestSetStackTop(const void *stackTop);

#endif // TEST_HOST_TEST_RTOS_H
