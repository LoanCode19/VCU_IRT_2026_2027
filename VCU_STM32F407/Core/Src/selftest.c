// Autotest de la carte nue (decision P10). Les modes et la lecture du resultat sont dans selftest.h.

#include "selftest.h"

#include <string.h>

#include "canIds.h"
#include "throttle.h"

#if (VCU_SELFTEST != 0) || defined(SELFTEST_HOST)

static void PutBigEndian(uint8_t *dst, uint32_t value, unsigned bytes)
{
  for (unsigned i = 0U; i < bytes; i++)
  {
    dst[i] = (uint8_t)(value >> (8U * (bytes - 1U - i)));
  }
}

static CanFrame_t MakeFrame(uint32_t canId, uint8_t dlc)
{
  CanFrame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.id = canId;
  frame.dlc = dlc;
  return frame;
}

void SelfTest_BuildFrames(CanFrame_t frames[SELFTEST_FRAME_COUNT])
{
  // APPS a 40 % de leur course, d'apres la calibration de initThrottleValues() (appelee par MainApp)
  const int32_t apps1 = (int32_t)(potmin[0] + (potmax[0] - potmin[0]) * 0.4f);
  const int32_t apps2 = (int32_t)(potmin[1] + (potmax[1] - potmin[1]) * 0.4f);
  const uint32_t sensorIds[6] = {CAN_ID_APPS1,    CAN_ID_APPS2,    CAN_ID_WHEEL_FR,
                                 CAN_ID_WHEEL_FL, CAN_ID_WHEEL_RR, CAN_ID_WHEEL_RL};
  const int32_t sensorValues[6] = {apps1, apps2, 0, 0, 0, 0};

  // Format provisoire des capteurs : la valeur sur 4 octets, big-endian
  for (unsigned i = 0U; i < 6U; i++)
  {
    frames[i] = MakeFrame(sensorIds[i], 4U);
    PutBigEndian(frames[i].data, (uint32_t)sensorValues[i], 4U);
  }
  // Onduleur DTI : temperatures en 0,1 degre (octets 0-1 onduleur, 2-3 moteur), octet 4 = pas de defaut
  frames[6] = MakeFrame(CAN_ID_DTI_TEMPERATURES, 8U);
  PutBigEndian(&frames[6].data[0], 400U, 2U);
  PutBigEndian(&frames[6].data[2], 400U, 2U);
  // Tension DC en volts, octets 6-7
  frames[7] = MakeFrame(CAN_ID_DTI_ERPM_VOLTAGE, 8U);
  PutBigEndian(&frames[7].data[6], 350U, 2U);
}

#endif

#ifndef SELFTEST_HOST
#if VCU_SELFTEST != 0

#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "TaskCanRX.h"
#include "TaskCanTx.h"
#include "stm32Interface.h"

#define SELFTEST_PERIOD_MS 10U        // une salve toutes les 10 ms, comme les capteurs
#define SELFTEST_PRINT_MS 1000U       // une ligne par seconde sur la console SWV
#define SELFTEST_HOG_AFTER_MS 5000U   // mode 4 : debut du blocage
#define SELFTEST_STACK_WORDS 512U     // vsnprintf et sa ligne de 256 octets
#define SELFTEST_PRIORITY (tskIDLE_PRIORITY + 2U) // sous les taches du VCU qui traitent les trames
#define SELFTEST_ITM_PORT 1U          // console SWV : a part des traces du VCU (port 0, printf)

extern volatile uint8_t lastResetByIwdg; // main.c : 1 si le dernier redemarrage vient de l'IWDG

static CAN_HandleTypeDef *sCan = NULL;
static uint32_t sSent = 0U;    // trames acceptees par le controleur CAN
static uint32_t sRefused = 0U; // trames abandonnees : boites d'emission occupees

void SelfTest_ConfigureCan(CAN_HandleTypeDef *hcan)
{
  sCan = hcan;
#if VCU_SELFTEST == SELFTEST_CAN_LOOPBACK
  // Boucle interne : le controleur recoit ses propres trames, sans rien sortir sur les broches
  hcan->Init.Mode = CAN_MODE_SILENT_LOOPBACK;
  if (HAL_CAN_Init(hcan) != HAL_OK)
  {
    Error_Handler();
  }
#endif
}

#if (VCU_SELFTEST == SELFTEST_CAN_LOOPBACK) || (VCU_SELFTEST == SELFTEST_CAN_NO_TRANSCEIVER)
// Envoie la salve en gardant une boite d'emission libre pour la consigne de TaskCanTx
static void SendFrames(const CanFrame_t frames[SELFTEST_FRAME_COUNT])
{
  for (unsigned i = 0U; i < SELFTEST_FRAME_COUNT; i++)
  {
    for (unsigned wait = 0U; (wait < 3U) && (HAL_CAN_GetTxMailboxesFreeLevel(sCan) < 2U); wait++)
    {
      vTaskDelay(1);
    }
    if ((HAL_CAN_GetTxMailboxesFreeLevel(sCan) >= 2U) && CanTx_Send(&frames[i]))
    {
      sSent++;
    }
    else
    {
      sRefused++;
    }
  }
}
#endif

// Comme ITM_SendChar() (core_cm4.h), mais sur le port 1. Rien n'est envoye si la console SWV est
// fermee ou si le port 1 n'y est pas coche.
static void PrintItm(const char *fmt, ...)
{
  char line[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if ((len <= 0) || ((ITM->TCR & ITM_TCR_ITMENA_Msk) == 0UL) || ((ITM->TER & (1UL << SELFTEST_ITM_PORT)) == 0UL))
  {
    return;
  }
  if ((size_t)len >= sizeof(line))
  {
    len = (int)sizeof(line) - 1; // ligne tronquee
  }
  for (int i = 0; i < len; i++)
  {
    while (ITM->PORT[SELFTEST_ITM_PORT].u32 == 0UL)
    {
      __NOP();
    }
    ITM->PORT[SELFTEST_ITM_PORT].u8 = (uint8_t)line[i];
  }
}

static void PrintStatus(TickType_t now)
{
  PrintItm("autotest %d, t = %lu s : tx %lu, refus %lu | rx : isr %lu, inconnu %lu, dlc %lu, plein %lu, "
           "pertes %lu/%lu/%lu, bus-off %lu | txErr %lu | tas min %u o\n",
           VCU_SELFTEST, (unsigned long)(now / configTICK_RATE_HZ), (unsigned long)sSent, (unsigned long)sRefused,
           (unsigned long)canRxStats.isrErrors, (unsigned long)canRxStats.unknownId,
           (unsigned long)canRxStats.badDlc, (unsigned long)canRxStats.rxQueueFull,
           (unsigned long)canRxStats.lostTrvp, (unsigned long)canRxStats.lostMain,
           (unsigned long)canRxStats.lostCooling, (unsigned long)canRxStats.busOff, (unsigned long)canTxErrors,
           (unsigned)xPortGetMinimumEverFreeHeapSize());
}

static void SelfTestTask(void *argument)
{
  (void)argument;
  CanFrame_t frames[SELFTEST_FRAME_COUNT];
  SelfTest_BuildFrames(frames);
  PrintItm("Autotest de la carte nue : mode %d (selftest.h). Dernier redemarrage : %s\n", VCU_SELFTEST,
           lastResetByIwdg ? "par le watchdog (IWDG)" : "normal");

  TickType_t wake = xTaskGetTickCount();
  TickType_t lastPrint = wake;
  for (;;)
  {
#if VCU_SELFTEST == SELFTEST_WATCHDOG
    if (wake >= pdMS_TO_TICKS(SELFTEST_HOG_AFTER_MS))
    {
      PrintItm("autotest 4 : la tache d'autotest accapare le processeur, l'IWDG doit redemarrer la carte\n");
      vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1U);
      for (;;)
      {
      }
    }
#elif (VCU_SELFTEST == SELFTEST_CAN_LOOPBACK) || (VCU_SELFTEST == SELFTEST_CAN_NO_TRANSCEIVER)
    SendFrames(frames);
#endif
    if ((wake - lastPrint) >= pdMS_TO_TICKS(SELFTEST_PRINT_MS))
    {
      lastPrint = wake;
      PrintStatus(wake);
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SELFTEST_PERIOD_MS));
  }
}

void SelfTest_Start(void)
{
  BaseType_t created = xTaskCreate(SelfTestTask, "SELFTEST", SELFTEST_STACK_WORDS, NULL, SELFTEST_PRIORITY, NULL);
  configASSERT(created == pdPASS);
}

#else // VCU_SELFTEST == 0 : autotest desactive

void SelfTest_ConfigureCan(CAN_HandleTypeDef *hcan)
{
  (void)hcan;
}

void SelfTest_Start(void)
{
}

#endif
#endif // SELFTEST_HOST
