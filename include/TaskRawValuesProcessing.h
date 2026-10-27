#ifndef TRVP_H
#define TRVP_H

void TaskRawValuesProcessing(void *pvParameters);
void updateWheelsDataTimeStatus(dataMessage_t wheelSpeeds[4], TickType_t timeNow);

typedef struct
{
    // int32_t ThrottleCommand; // calculated throttle command to be sent to the inverter
    uint32_t speed; // vitesse du vehicule, en mm/s (D7)

    dataMessage_t wheelSpeeds[4];
    dataMessage_t appsValues[2];

    bool brakePedalPressed; // true if brake pedal is pressed, false otherwise
    TickType_t lastCheckedBreakPedal;

    TickType_t lastCallTimeStmp;

} TRVPState_t;

typedef struct
{
    QueueHandle_t *xTRVPQueue;
    QueueHandle_t *xMainQueue;
    TaskId taskId;

} TRVPParams_t;

#endif