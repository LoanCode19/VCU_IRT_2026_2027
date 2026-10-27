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

    // Etat du vehicule, trame provisoire 0x012 (T19)
    dataMessage_t tsActive;
    dataMessage_t sdcClosed;
    dataMessage_t brake;
    dataMessage_t r2dSwitch;

    TickType_t lastCallTimeStmp;

    bool direction; // true for forward, false for reverse

} MainState_t;

#endif