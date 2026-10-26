// Secteur : reception CAN
// Code teste : TaskCanRx (src/TaskCanRX.c) en vrai, avec les queues de MainApp()
// Problemes : B1, B6, R2 de AUDIT_VCU.md (+ tests de non-regression NR)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "structs.h"
#include "canIds.h"
#include "queueHandles.h"
#include "TaskCanRX.h"
#include "test_app.h"
#include "vcu_test.h"

static CanRxParams_t sCanRxParams; // static : jamais sur la pile (cf. B5)

static void Setup(TickType_t now)
{
    BootMainApp();
    sCanRxParams = (CanRxParams_t){&xMainQueue, &xTRVPQueue, &xCanRxQueue, &xWDGQueue, TaskCanRxId, &xCoolingQueue};
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(now);
}

// Trame standard telle que l'ISR de la carte la remet au VCU, datee de maintenant
static bool PushFrame(uint32_t canId, uint8_t dlc, const uint8_t data[8])
{
    CanFrame_t frame = {.id = canId, .ide = 0U, .dlc = dlc, .rxTick = xTaskGetTickCount()};
    memcpy(frame.data, data, sizeof(frame.data));
    UBaseType_t before = uxTestQueueCount(xCanRxQueue);
    CanRx_OnFrameFromISR(&frame, NULL);
    return uxTestQueueCount(xCanRxQueue) > before;
}

static void PutBigEndian(uint8_t *dst, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++)
    {
        dst[i] = (uint8_t)(value >> (8U * (bytes - 1U - i)));
    }
}

// Format provisoire des capteurs : une valeur de 4 octets big-endian en tete de trame
static bool PushValue(uint32_t canId, int32_t value)
{
    uint8_t data[8] = {0};
    PutBigEndian(data, (uint32_t)value, 4U);
    return PushFrame(canId, 4U, data);
}

// Un signal du VCU, dans la trame qui le transporte sur le bus
static bool PushSignal(MessageIds appId, int32_t value)
{
    switch (appId)
    {
    case wheelFRId:
        return PushValue(CAN_ID_WHEEL_FR, value);
    case wheelFLId:
        return PushValue(CAN_ID_WHEEL_FL, value);
    case wheelRRId:
        return PushValue(CAN_ID_WHEEL_RR, value);
    case wheelRLId:
        return PushValue(CAN_ID_WHEEL_RL, value);
    case apps1Id:
        return PushValue(CAN_ID_APPS1, value);
    case apps2Id:
        return PushValue(CAN_ID_APPS2, value);
    case motor_tempId:
        return PushValue(CAN_ID_MOTOR_TEMP, value);
    case inverter_tempId:
        return PushValue(CAN_ID_INVERTER_TEMP, value);
    case voltageId:
        return PushValue(CAN_ID_VOLTAGE, value);
    default:
        return false;
    }
}

// Vide la queue et dit si elle contenait un message appId de valeur value
static bool HasMessage(QueueHandle_t queue, MessageIds appId, int32_t value)
{
    bool found = false;
    dataMessage_t msg = {0};
    while (xQueueReceive(queue, &msg, 0) == pdPASS)
    {
        if ((msg.id == appId) && (msg.data == value))
        {
            found = true;
        }
    }
    return found;
}

static void RunCanRx(void)
{
    RunTaskOnce(TaskCanRx, &sCanRxParams);
}

static void TestCanRx_Timestamp(void)
{
    Setup(1234U);
    (void)PushSignal(apps1Id, 2000);
    RunCanRx();

    dataMessage_t msg = {0};
    if (!vt_check("NR-1", "-", xQueueReceive(xTRVPQueue, &msg, 0) == pdPASS && msg.id == apps1Id,
                  "trame APPS1 -> message APPS1 vers TRVP"))
    {
        vt_blocked("B1a", "T 11.9.2.d", "aucun message APPS1 recu, horodatage non verifiable");
        return;
    }
    vt_check("B1a", "T 11.9.2.d", msg.timestamp == 1234U,
             "trame recue a t=1234 ms -> timestamp du message %lu (attendu 1234)", (unsigned long)msg.timestamp);
}

static void TestCanRx_Routing(void)
{
    static const struct
    {
        MessageIds appId;
        bool toTrvp;
        const char *name;
    } routes[] = {
        {wheelFRId, true, "roue FR"}, {wheelFLId, true, "roue FL"}, {wheelRRId, true, "roue RR"},
        {wheelRLId, true, "roue RL"}, {apps1Id, true, "APPS1"}, {apps2Id, true, "APPS2"},
        {motor_tempId, false, "temperature moteur"}, {inverter_tempId, false, "temperature onduleur"},
        {voltageId, false, "tension"}};

    bool allOk = true;
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
    {
        Setup(100U);
        (void)PushSignal(routes[i].appId, 42);
        RunCanRx();
        QueueHandle_t expected = routes[i].toTrvp ? xTRVPQueue : xMainQueue;
        if (!HasMessage(expected, routes[i].appId, 42))
        {
            allOk = false;
            vt_note("%s mal route", routes[i].name);
        }
    }
    vt_check("NR-2", "-", allOk, "9 signaux recus routes vers la bonne tache avec le bon identifiant");
}

static void TestCanRx_Decoding(void)
{
    Setup(100U);
    uint32_t aliasId = CAN_ID_APPS1 | 0x100U; // ID sur 11 bits dont l'octet bas est celui de APPS1
    (void)PushValue(aliasId, 77);
    RunCanRx();
    UBaseType_t forwarded = uxTestQueueCount(xTRVPQueue) + uxTestQueueCount(xMainQueue);
    vt_check("B6a", "T 11.9.2.d", forwarded == 0U,
             "trame d'ID 0x%03lX (inconnue) -> %u message(s) transmis (attendu 0 ; tronque a 8 bits, l'ID est celui de APPS1)",
             (unsigned long)aliasId, (unsigned)forwarded);

    unsigned idBits = (unsigned)(sizeof(((CanFrame_t *)0)->id) * 8U);
    vt_check("B6b", "-", idBits >= 11U, "CanFrame_t.id sur %u bits (attendu >= 11 pour un ID standard)", idBits);

    unsigned dataBytes = (unsigned)sizeof(((CanFrame_t *)0)->data);
    vt_check("B6c", "T 11.9.2.d", dataBytes >= 8U,
             "CanFrame_t transporte %u octets et la longueur (DLC) de la trame (attendu 8 octets + DLC)", dataBytes);
}

static void TestCanRx_Overload(void)
{
    // TRVP en retard : sa queue est pleine quand de nouvelles valeurs APPS arrivent
    Setup(100U);
    dataMessage_t old = {.id = wheelFRId, .data = 1, .timestamp = 50, .timeStatus = STATUS_TIME_OK};
    while (xQueueSend(xTRVPQueue, &old, 0) == pdPASS)
    {
    }
    (void)PushSignal(apps1Id, 1000);
    (void)PushSignal(apps1Id, 2000);
    (void)PushSignal(apps1Id, 3000);
    RunCanRx();

    bool latestAvailable = HasMessage(xTRVPQueue, apps1Id, 3000);
    vt_check("R2a", "T 11.9.2.d", latestAvailable,
             "queue TRVP pleine, 3 nouvelles valeurs APPS1 -> derniere valeur %s (attendu disponible, sinon perte silencieuse)",
             latestAvailable ? "disponible" : "perdue");
}

int main(void)
{
    vt_begin("Reception CAN");

    TestCanRx_Timestamp();
    TestCanRx_Routing();
    TestCanRx_Decoding();
    TestCanRx_Overload();

    return vt_end();
}
