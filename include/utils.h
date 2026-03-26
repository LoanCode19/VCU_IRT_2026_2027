
#include "FreeRTOS.h"
#include "structs.h"

float GetUserThrottleCommand(dataMessage_t *pot1val, dataMessage_t *pot2val, int speed, bool brake);
float ProcessThrottle(float finalSpnt, int speed, int motorTemp, int inverterTemp, float voltage);
void updateDataTimeStatus(dataMessage_t *dataMsg, int count, int maxDiff, TickType_t now);
void sort4(int32_t v[4], uint8_t n);
int calculateSpeed(dataMessage_t wheelSpeeds[4], int wheelCount);
//  float checkMessageTimeStamps(const MotorControlState_t *motorState, GlobalState_t *globalState, TickType_t time_now);

inline static int32_t change(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline static float changeFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline static void swap(int32_t *a, int32_t *b)
{
    if (*a > *b)
    {
        int32_t tmp = *a;
        *a = *b;
        *b = tmp;
    }
}
