
#include "structs.h"

#ifndef THROTTLE_H
#define THROTTLE_H

bool CheckAndLimitRange(int *potval, int potIdx);
float NormalizeThrottle(int potval, int potIdx);
bool TemperatureDerate(int temp, int tempMax, float *finalSpnt);
float RampThrottle(float finalSpnt);
float CalcThrottle(float potnom, int speed, bool brake);

float CalcIdleSpeed(int speed);
float CalcCruiseSpeed(int speed);
void UdcLimitCommand(float *finalSpnt, float udc);
void IdcLimitCommand(float *finalSpnt, float idc);
void SpeedLimitCommand(float *finalSpnt, int speed);
extern int potmin[2];
extern int potmax[2];
extern float regenRpm;
extern float brknompedal;
extern float regenmax;
extern float regenBrake;
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
extern float regenRamp;
extern float actualThrottleRamp;
extern int bmslimhigh;
extern int bmslimlow;
extern int accelmax;
extern int accelflt;
extern float udcmin;
extern float udcmax;
extern float idcmin;
extern float idcmax;
extern int speedLimit;
extern float regenendRpm;
extern float ThrotRpmFilt;
extern int speedFiltered;
extern float potnomFiltered;
extern float brkRamped;

extern int throtRampRpm;
extern float throtRamp;
extern float throtRampMax;

extern float motorTempMax;
extern float inverterTempMax;

#endif // THROTTLE_H