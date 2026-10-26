#include <semphr.h>
#ifndef CAN_RX_H
#define CAN_RX_H

#include "structs.h"

void TaskCanRx(void *pvParameters);

// Compteurs de la reception CAN, lisibles au debogueur : rien n'est perdu sans etre compte
typedef struct
{
    uint32_t isrErrors; // lecture du FIFO en echec dans l'ISR de la carte
    uint32_t unknownId; // ID absent de la table, ou trame etendue
    uint32_t badDlc;    // trame trop courte pour un de ses signaux
    // Queue pleine : la plus ancienne trame ou le plus ancien message a laisse sa place (T13)
    uint32_t rxQueueFull;
    uint32_t lostTrvp;
    uint32_t lostMain;
    uint32_t lostCooling;
} CanRxStats_t;

extern volatile CanRxStats_t canRxStats;

// Destination d'un signal recu
typedef enum
{
    CAN_TO_TRVP,             // capteurs : TaskRawValuesProcessing
    CAN_TO_MAIN,             // TaskMain
    CAN_TO_MAIN_AND_COOLING, // TaskMain et TaskCoolingSystem (temperatures)
} CanRoute_t;

// Une ligne de la table de decodage : un signal dans une trame standard
typedef struct
{
    uint32_t id;       // ID standard (11 bits)
    uint8_t startByte; // premier octet du signal
    uint8_t length;    // 1, 2 ou 4 octets, big-endian
    bool isSigned;
    int32_t divisor;   // valeur = brut / divisor, arrondie au plus proche (10 pour 0,1 degre)
    MessageIds appId;
    CanRoute_t route;
} CanRxSignal_t;

extern const CanRxSignal_t canRxTable[];
extern const unsigned canRxTableSize;

// Point d'entree des trames recues, appele par l'ISR de la carte (et par l'emulateur)
void CanRx_OnFrameFromISR(const CanFrame_t *frame, BaseType_t *higherPriorityTaskWoken);

extern SemaphoreHandle_t xCanRxSemaphore;

typedef struct
{
    QueueHandle_t *xMainQueue;
    QueueHandle_t *xTRVPQueue;
    QueueHandle_t *xCanRxQueue;
    QueueHandle_t *xWDGQueue;
    TaskId taskId;
    QueueHandle_t *xCoolingQueue; // en dernier : une initialisation sans ce champ le laisse a NULL

} CanRxParams_t;

#endif