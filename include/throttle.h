#ifndef THROTTLE_H
#define THROTTLE_H

#include <stdbool.h>
#include <stdint.h>

#include "structs.h"

// Pedale d'accelerateur (APPS)
bool AppsInRange(int32_t raw, int potIdx);
int32_t AppsClamp(int32_t raw, int potIdx);
float NormalizeThrottle(int potval, int potIdx);

// Calcul de la consigne, en % (appeles par GetUserThrottleCommand() et ProcessThrottle(), dans utils.c)
float CalcThrottle(float potnom, int32_t speed, bool brake);
float RampThrottle(float potnom, float rampPerS, uint32_t elapsedMs);
bool TemperatureDerate(int temp, int tempMax, float *finalSpnt);
void UdcLimitCommand(float *finalSpnt, float udc);
void SpeedLimitCommand(float *finalSpnt, float speedMs);

// Calibration, reglee par initThrottleValues() (VCU.c)
extern int potmin[2];
extern int potmax[2];
extern float throtmax;
extern float throtmin;
extern float throtdead;
extern float udcmin;
extern float udcDerateMarginV;
extern float speedLimitMs;
extern float speedLimitMarginMs;
extern float ThrotRpmFilt;
extern float throtRampSpeedMs;
extern float throtRampPerS;
extern float throtRampMaxPerS;
extern float motorTempMax;
extern float inverterTempMax;

#endif // THROTTLE_H
