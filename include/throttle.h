
#include "structs.h"

#ifndef THROTTLE_H
#define THROTTLE_H

bool AppsInRange(int32_t raw, int potIdx);
int32_t AppsClamp(int32_t raw, int potIdx);
float NormalizeThrottle(int potval, int potIdx);
bool TemperatureDerate(int temp, int tempMax, float *finalSpnt);
float RampThrottle(float potnom, float rampPerS, uint32_t elapsedMs);
float CalcThrottle(float potnom, int speed, bool brake);

float CalcIdleSpeed(int speed);
float CalcCruiseSpeed(int speed);
void UdcLimitCommand(float *finalSpnt, float udc);
void IdcLimitCommand(float *finalSpnt, float idc);
void SpeedLimitCommand(float *finalSpnt, float speedMs);
extern int potmin[2];
extern int potmax[2];
extern float brknompedal;
extern float brkcruise;
extern float throtmax;
extern float throtmaxRev;
extern float throtmin;
extern float throtdead;
extern int idleSpeed;
extern int cruiseSpeed;
extern float speedkp;
extern int speedflt;
extern float idleThrotLim;
extern int bmslimhigh;
extern int bmslimlow;
extern int accelmax;
extern int accelflt;
extern float udcmin;
extern float udcmax;
extern float idcmin;
extern float idcmax;
extern float speedLimitMs;
extern float speedLimitMarginMs;
extern float ThrotRpmFilt;
extern float potnomFiltered;
extern float brkRamped;

extern float throtRampSpeedMs;
extern float throtRampPerS;
extern float throtRampMaxPerS;

extern float motorTempMax;
extern float inverterTempMax;

#endif // THROTTLE_H