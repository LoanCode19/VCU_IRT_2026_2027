#ifndef CAN_TX_H
#define CAN_TX_H

void TaskCanTx(void *pvParameters);

typedef struct
{
    QueueHandle_t *xCanTxQueue;
    QueueHandle_t *xWDGQueue;

    TaskId taskId;

} CanTxParam_t;

#endif