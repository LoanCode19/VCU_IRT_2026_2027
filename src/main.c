#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>

#include "FreeRTOS.h"
#include "task.h"

#include "console.h"

#ifdef BUILD_DIR
#define BUILD BUILD_DIR
#else
#define BUILD "./"
#endif

static void handle_sigint(int signal);

extern void MainApp();

int main(void)
{
   /* SIGINT is not blocked by the posix port */
   signal(SIGINT, handle_sigint);

   console_init();
   console_print("Starting echo blinky demo\n");

   MainApp();

   return 0;
}

void vApplicationMallocFailedHook(void)
{
   vAssertCalled(__FILE__, __LINE__);
}

void vApplicationIdleHook(void)
{
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
   (void)pcTaskName;
   (void)pxTask;
   vAssertCalled(__FILE__, __LINE__);
}

void vAssertCalled(const char *const pcFileName, unsigned long ulLine)
{
   taskENTER_CRITICAL();
   {
      while (1)
      {
         __asm volatile("NOP");
         __asm volatile("NOP");
      }
   }
   taskEXIT_CRITICAL();
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{
   static StaticTask_t xIdleTaskTCB;
   static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
   *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
   *ppxIdleTaskStackBuffer = uxIdleTaskStack;
   *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void handle_sigint(int signal)
{
   int xReturn;

   xReturn = chdir(BUILD); /* changing dir to place gmon.out inside build */

   if (xReturn == -1)
   {
      printf("chdir into %s error is %d\n", BUILD, errno);
   }

   exit(2);
}
