#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "project_config.h"

#ifndef STRUCTS_H
#define STRUCTS_H
// enums
typedef enum
{
    STATUS_TIME_OK = 0,
    STATUS_TIMEOUT,
    STATUS_NOT_READY,
} DataTimeStatus_t;

// MUST BEGIN AT ZERO
typedef enum TasksIds
{
    TaskCanRxId,
    TaskTRVPId,
    TaskIHMId,
    TaskCanTxId,
    TaskMainId,

    TASK_COUNT, // nombre de taches surveillees par le watchdog : toujours en dernier
} TaskId; // MUST BEGIN AT ZERO
// enums

typedef enum messageIds
{
    wheelFRId,
    wheelFLId,
    wheelRRId,
    wheelRLId,

    apps1Id,
    apps2Id,

    motor_tempId,
    inverter_tempId,
    voltageId,

    calculatedSpeedId,
    processedThrottleId,

    inverterTorqueCmdId,

} MessageIds;

typedef struct
{
    TaskId taskId;
    TickType_t timestamp;
} WdgMessage_t;

typedef struct
{
    MessageIds id;
    int32_t data;
    TickType_t timestamp;
    DataTimeStatus_t timeStatus;

} dataMessage_t;

typedef struct
{
    uint8_t id;
    uint32_t data;
} dataCan;

typedef TickType_t WDGTimoutTable[MAX_TASKS];

typedef enum speedStatus
{
    SPEED_VALID,
    SPEED_DEGRADED,
    SPEED_INVALID

} SpeedStatus;

typedef enum Order
{
    FaultLight = 0,
    CoolingPump = 1,
    CoolingFans = 2,
    R2Dbuzzer = 3,
    HBLed = 4,

} Order_t;

typedef struct
{
    Order_t order;
    bool state;

} IHMOrder_t;

typedef struct
{
    Order_t order;
    bool state;

} DigitalOrder_t;

#endif // STRUCTS_H