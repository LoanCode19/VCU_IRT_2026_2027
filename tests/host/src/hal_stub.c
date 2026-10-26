// Fausse HAL STM32 du banc (hal_stub.h) : un FIFO de reception de 3 trames, comme le materiel, un
// journal des emissions et des compteurs, pour tester sur PC le code CAN de la carte.

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hal_stub.h"

#define HAL_STUB_FIFO_DEPTH 3U         // profondeur d'un FIFO de reception du bxCAN
#define HAL_STUB_MAX_FAILED_READS 10U  // lectures en echec de suite : l'appelant boucle

typedef struct
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
} HalStubRxFrame_t;

static HalStubRxFrame_t sFifo[HAL_STUB_FIFO_DEPTH];
static unsigned sFifoCount = 0U;
static bool sRxReadFails = false;
static unsigned sFailedReadsInARow = 0U;
static jmp_buf *sLoopEscape = NULL;

static bool sTxFails = false;
static unsigned sTxCount = 0U;
static CAN_TxHeaderTypeDef sLastTxHeader;
static uint8_t sLastTxData[8];

static unsigned sStartCount = 0U;
static uint32_t sNotifications = 0U;
static unsigned sErrorHandlerCount = 0U;
static unsigned sIwdgRefreshCount = 0U;

void vHalStubReset(void)
{
    sFifoCount = 0U;
    sRxReadFails = false;
    sFailedReadsInARow = 0U;
    sLoopEscape = NULL;
    sTxFails = false;
    sTxCount = 0U;
    memset(&sLastTxHeader, 0, sizeof(sLastTxHeader));
    memset(sLastTxData, 0, sizeof(sLastTxData));
    sStartCount = 0U;
    sNotifications = 0U;
    sErrorHandlerCount = 0U;
    sIwdgRefreshCount = 0U;
}

bool xHalStubPushRx(const CAN_RxHeaderTypeDef *header, const uint8_t data[8])
{
    if (sFifoCount >= HAL_STUB_FIFO_DEPTH)
    {
        return false;
    }
    sFifo[sFifoCount].header = *header;
    memcpy(sFifo[sFifoCount].data, data, sizeof(sFifo[sFifoCount].data));
    sFifoCount++;
    return true;
}

void vHalStubSetRxReadFails(bool fails)
{
    sRxReadFails = fails;
    sFailedReadsInARow = 0U;
}

void vHalStubSetLoopEscape(jmp_buf *env)
{
    sLoopEscape = env;
}

void vHalStubSetTxFails(bool fails)
{
    sTxFails = fails;
}

unsigned uxHalStubTxCount(void)
{
    return sTxCount;
}

bool xHalStubLastTx(CAN_TxHeaderTypeDef *header, uint8_t data[8])
{
    if (sTxCount == 0U)
    {
        return false;
    }
    *header = sLastTxHeader;
    memcpy(data, sLastTxData, sizeof(sLastTxData));
    return true;
}

unsigned uxHalStubStartCount(void)
{
    return sStartCount;
}

uint32_t ulHalStubNotifications(void)
{
    return sNotifications;
}

unsigned uxHalStubErrorHandlerCount(void)
{
    return sErrorHandlerCount;
}

unsigned uxHalStubIwdgRefreshCount(void)
{
    return sIwdgRefreshCount;
}

// ----------------------------------------------------------------------------------------------
// Fonctions de la HAL

HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *hcan)
{
    (void)hcan;
    sStartCount++;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *hcan, uint32_t ActiveITs)
{
    (void)hcan;
    sNotifications |= ActiveITs;
    return HAL_OK;
}

uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *hcan, uint32_t RxFifo)
{
    (void)hcan;
    return (RxFifo == CAN_RX_FIFO0) ? sFifoCount : 0U;
}

HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *hcan, uint32_t RxFifo, CAN_RxHeaderTypeDef *pHeader,
                                       uint8_t aData[])
{
    (void)hcan;
    if (sRxReadFails || (RxFifo != CAN_RX_FIFO0) || (sFifoCount == 0U))
    {
        sFailedReadsInARow++;
        if ((sLoopEscape != NULL) && (sFailedReadsInARow >= HAL_STUB_MAX_FAILED_READS))
        {
            longjmp(*sLoopEscape, 1);
        }
        return HAL_ERROR;
    }
    sFailedReadsInARow = 0U;
    // Comme la vraie HAL : les 8 octets sont copies, quel que soit le DLC
    *pHeader = sFifo[0].header;
    memcpy(aData, sFifo[0].data, sizeof(sFifo[0].data));
    memmove(&sFifo[0], &sFifo[1], (sFifoCount - 1U) * sizeof(sFifo[0]));
    sFifoCount--;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, const CAN_TxHeaderTypeDef *pHeader,
                                       const uint8_t aData[], uint32_t *pTxMailbox)
{
    if (sTxFails)
    {
        hcan->ErrorCode |= HAL_CAN_ERROR_PARAM; // comme la vraie HAL quand les 3 boites sont pleines
        return HAL_ERROR;
    }
    sLastTxHeader = *pHeader;
    memcpy(sLastTxData, aData, sizeof(sLastTxData));
    sTxCount++;
    if (pTxMailbox != NULL)
    {
        *pTxMailbox = 1U;
    }
    return HAL_OK;
}

uint32_t HAL_CAN_GetError(const CAN_HandleTypeDef *hcan)
{
    return hcan->ErrorCode;
}

HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *hiwdg)
{
    (void)hiwdg;
    sIwdgRefreshCount++;
    return HAL_OK;
}

uint32_t ITM_SendChar(uint32_t ch)
{
    return ch;
}

// Sur la carte : interruptions coupees et boucle infinie. Ici : seulement compte.
void Error_Handler(void)
{
    sErrorHandlerCount++;
}
