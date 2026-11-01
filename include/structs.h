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
    TaskDigitalSignalsId,
    TaskCoolingId,

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

    // Etat du vehicule (trame provisoire 0x012 en attendant D5, T19) : 0 ou 1
    tsActiveId,     // TS actif
    sdcClosedId,    // circuit d'arret ferme
    brakePressedId, // frein appuye
    r2dSwitchId,    // interrupteur R2D sur ON (T22)

    appsFaultId,      // TRVP -> Main : faute APPS confirmee (S1, D2), pour le voyant defaut
    driveEnableCmdId, // Main -> CanTx : "Drive enable" du DTI (T20)

} MessageIds;

typedef struct
{
    MessageIds id;
    int32_t data;
    TickType_t timestamp;
    DataTimeStatus_t timeStatus;

} dataMessage_t;

// Trame CAN recue, telle que l'ISR de la carte la remet au VCU (CanRx_OnFrameFromISR)
typedef struct
{
    uint32_t id;       // 11 bits (standard) ou 29 bits (etendu)
    uint8_t ide;       // 0 = standard, 1 = etendu
    uint8_t dlc;       // nombre d'octets recus
    uint8_t data[8];
    TickType_t rxTick; // heure de reception, prise dans l'ISR
} CanFrame_t;

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