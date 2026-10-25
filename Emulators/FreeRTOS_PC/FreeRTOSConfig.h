#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// Configuration FreeRTOS de l'emulateur PC (port Windows MSVC-MingW, noyau V10.3.1).
// Les valeurs reprennent celles de la carte (VCU_STM32F407/Core/Inc/FreeRTOSConfig.h) quand le
// port le permet, pour que le code VCU se comporte comme sur le STM32. Ecarts : voir les commentaires.

#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0 // carte : 1 (instruction CLZ du Cortex-M)
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES (20)
#define configMINIMAL_STACK_SIZE ((uint16_t)128)
#define configTOTAL_HEAP_SIZE ((size_t)40960) // comme la carte : un manque de memoire se voit aussi ici
#define configMAX_TASK_NAME_LEN (16)
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configQUEUE_REGISTRY_SIZE 8
#define configUSE_TIMERS 0
#define configUSE_CO_ROUTINES 0
#define configMAX_CO_ROUTINE_PRIORITIES (2)
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configMESSAGE_BUFFER_LENGTH_TYPE size_t

// Sur PC, chaque tache est un thread Windows avec sa propre pile : pas de detection de debordement.
#define configCHECK_FOR_STACK_OVERFLOW 0
#define configUSE_MALLOC_FAILED_HOOK 1

#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskCleanUpResources 0
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 0

// Espion des queues (emu_trace.c) : chaque message depose par une tache (xQueueSend) est compte,
// pour que les tests observent le VCU de l'exterieur. pvItemToQueue est le parametre de
// xQueueGenericSend, seule fonction du noyau qui appelle traceQUEUE_SEND sans queue sets.
void EmuTrace_QueueSend(const void *queue, const void *item, unsigned long itemSize);
#define traceQUEUE_SEND(pxQueue) EmuTrace_QueueSend((pxQueue), pvItemToQueue, (pxQueue)->uxItemSize)

// Assertion : message + arret du programme avec le code 3 (voir main_pc.c)
void vAssertCalled(const char *file, unsigned long line);
#define configASSERT(x)                        \
    if ((x) == 0)                              \
    {                                          \
        vAssertCalled(__FILE__, __LINE__);     \
    }

#endif // FREERTOS_CONFIG_H
