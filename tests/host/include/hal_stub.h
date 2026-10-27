#ifndef TEST_HOST_HAL_STUB_H
#define TEST_HOST_HAL_STUB_H

// Fausse HAL STM32 du banc (src/hal_stub.c) : juste ce que la zone USER CODE 4 de
// VCU_STM32F407/Core/Src/main.c utilise (CAN, IWDG, ITM). Les types et les valeurs des constantes
// reprennent stm32f4xx_hal_can.h. Le bus est remplace par un FIFO de reception de 3 trames, comme
// le materiel, et par un journal des emissions, pilotes par les tests.

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    HAL_OK = 0x00U,
    HAL_ERROR = 0x01U,
    HAL_BUSY = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef enum
{
    DISABLE = 0U,
    ENABLE = !DISABLE
} FunctionalState;

typedef struct
{
    void *Instance;
    volatile uint32_t ErrorCode;
} CAN_HandleTypeDef;

typedef struct
{
    uint32_t StdId;
    uint32_t ExtId;
    uint32_t IDE;
    uint32_t RTR;
    uint32_t DLC;
    uint32_t Timestamp;
    uint32_t FilterMatchIndex;
} CAN_RxHeaderTypeDef;

typedef struct
{
    uint32_t StdId;
    uint32_t ExtId;
    uint32_t IDE;
    uint32_t RTR;
    uint32_t DLC;
    FunctionalState TransmitGlobalTime;
} CAN_TxHeaderTypeDef;

typedef struct
{
    void *Instance;
} IWDG_HandleTypeDef;

#define CAN_ID_STD 0x00000000U
#define CAN_ID_EXT 0x00000004U
#define CAN_RTR_DATA 0x00000000U
#define CAN_RX_FIFO0 0x00000000U
#define CAN_RX_FIFO1 0x00000001U

#define CAN_IT_TX_MAILBOX_EMPTY 0x00000001U
#define CAN_IT_RX_FIFO0_MSG_PENDING 0x00000002U
#define CAN_IT_RX_FIFO1_MSG_PENDING 0x00000010U
#define CAN_IT_ERROR_WARNING 0x00000100U
#define CAN_IT_ERROR_PASSIVE 0x00000200U
#define CAN_IT_BUSOFF 0x00000400U
#define CAN_IT_LAST_ERROR_CODE 0x00000800U
#define CAN_IT_ERROR 0x00008000U

#define HAL_CAN_ERROR_NONE 0x00000000U
#define HAL_CAN_ERROR_EWG 0x00000001U
#define HAL_CAN_ERROR_EPV 0x00000002U
#define HAL_CAN_ERROR_BOF 0x00000004U
#define HAL_CAN_ERROR_ACK 0x00000020U
#define HAL_CAN_ERROR_PARAM 0x00200000U

HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *hcan, uint32_t ActiveITs);
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *hcan, uint32_t RxFifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *hcan, uint32_t RxFifo, CAN_RxHeaderTypeDef *pHeader,
                                       uint8_t aData[]);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, const CAN_TxHeaderTypeDef *pHeader,
                                       const uint8_t aData[], uint32_t *pTxMailbox);
uint32_t HAL_CAN_GetError(const CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *hiwdg);
uint32_t ITM_SendChar(uint32_t ch);
void Error_Handler(void);

// ----------------------------------------------------------------------------------------------
// Commandes reservees aux tests

void vHalStubReset(void); // FIFO vide, emission acceptee, compteurs a zero

// Trame recue dans le FIFO0. false si le FIFO est plein (3 trames) : le materiel la perdrait.
bool xHalStubPushRx(const CAN_RxHeaderTypeDef *header, const uint8_t data[8]);

// true : toute lecture du FIFO echoue et la trame reste dans le FIFO.
void vHalStubSetRxReadFails(bool fails);

// Si l'appelant relit le FIFO 10 fois de suite apres un echec, il boucle dans l'interruption :
// longjmp vers env au lieu de bloquer le banc. NULL : desactive.
void vHalStubSetLoopEscape(jmp_buf *env);

void vHalStubSetTxFails(bool fails);                         // true : HAL_CAN_AddTxMessage refuse (boites pleines)
unsigned uxHalStubTxCount(void);                             // trames acceptees depuis vHalStubReset()
bool xHalStubLastTx(CAN_TxHeaderTypeDef *header, uint8_t data[8]); // derniere trame acceptee

unsigned uxHalStubStartCount(void);     // appels a HAL_CAN_Start
uint32_t ulHalStubNotifications(void);  // notifications activees (HAL_CAN_ActivateNotification)
unsigned uxHalStubErrorHandlerCount(void);
unsigned uxHalStubIwdgRefreshCount(void);

#endif // TEST_HOST_HAL_STUB_H
