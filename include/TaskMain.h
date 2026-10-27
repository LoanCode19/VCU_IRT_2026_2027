#ifndef MAIN_H
#define MAIN_H

void TaskMain(void *pvParameters);

typedef struct
{
    QueueHandle_t *xMainQueue;
    QueueHandle_t *xTRVPQueue;
    QueueHandle_t *xIHMQueue;
    QueueHandle_t *xCanTxQueue;
    TaskId taskId;
} MainParams_t;

typedef struct
{
    dataMessage_t ThrottleCommand; // calculated throttle command to be sent to the inverter
    dataMessage_t speed;           // vitesse du vehicule, en mm/s (D7)

    dataMessage_t motorTemp;
    dataMessage_t inverterTemp;
    dataMessage_t Voltage;

    bool brakePedalPressed; // true if brake pedal is pressed, false otherwise
    TickType_t lastCheckedBreakPedal;

    TickType_t lastCallTimeStmp;

    bool direction; // true for forward, false for reverse

} MainState_t;

#endif