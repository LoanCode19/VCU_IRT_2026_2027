// Secteur : reception CAN
// Code teste : TaskCanRx (src/TaskCanRX.c) en vrai, avec les queues de MainApp()
// Problemes : B1, B6, R2 de AUDIT_VCU.md (+ tests de non-regression NR)

#include <stdbool.h>
#include <stdint.h>

#include "structs.h"
#include "queueHandles.h"
#include "TaskCanRX.h"
#include "test_app.h"
#include "vcu_test.h"

static CanRxParams_t sCanRxParams; // static : jamais sur la pile (cf. B5)

static void Setup(TickType_t now)
{
    BootMainApp();
    sCanRxParams = (CanRxParams_t){&xMainQueue, &xTRVPQueue, &xCanRxQueue, &xWDGQueue, TaskCanRxId};
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(now);
}

// Trame telle que l'ISR de VCU_STM32F407/Core/Src/main.c la depose aujourd'hui
static bool PushFrame(uint32_t canId, uint32_t data)
{
    dataCan frame = {0};
    frame.id = canId; // uint8_t : un ID sur 11 bits est tronque ici (B6)
    frame.data = data;
    return xQueueSendFromISR(xCanRxQueue, &frame, NULL) == pdPASS;
}

static void RunCanRx(void)
{
    RunTaskOnce(TaskCanRx, &sCanRxParams);
}

static void TestCanRx_Timestamp(void)
{
    Setup(1234U);
    (void)PushFrame(0x30, 2000);
    RunCanRx();

    dataMessage_t msg = {0};
    if (!vt_check("NR-1", "-", xQueueReceive(xTRVPQueue, &msg, 0) == pdPASS && msg.id == apps1Id,
                  "trame 0x30 -> message APPS1 vers TRVP"))
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
        uint32_t canId;
        MessageIds appId;
        bool toTrvp;
    } routes[] = {
        {0x20, wheelFRId, true}, {0x21, wheelFLId, true}, {0x22, wheelRRId, true}, {0x23, wheelRLId, true},
        {0x30, apps1Id, true}, {0x31, apps2Id, true},
        {0x40, motor_tempId, false}, {0x41, inverter_tempId, false}, {0x42, voltageId, false}};

    bool allOk = true;
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
    {
        Setup(100U);
        (void)PushFrame(routes[i].canId, 42U);
        RunCanRx();
        dataMessage_t msg = {0};
        QueueHandle_t expected = routes[i].toTrvp ? xTRVPQueue : xMainQueue;
        bool ok = (xQueueReceive(expected, &msg, 0) == pdPASS) && (msg.id == routes[i].appId) && (msg.data == 42);
        if (!ok)
        {
            allOk = false;
            vt_note("trame 0x%02lX mal routee", (unsigned long)routes[i].canId);
        }
    }
    vt_check("NR-2", "-", allOk, "9 ID CAN connus routes vers la bonne tache avec le bon identifiant");
}

static void TestCanRx_Decoding(void)
{
    Setup(100U);
    (void)PushFrame(0x120, 77U); // ID standard 11 bits inconnu du VCU
    RunCanRx();
    UBaseType_t forwarded = uxTestQueueCount(xTRVPQueue) + uxTestQueueCount(xMainQueue);
    vt_check("B6a", "T 11.9.2.d", forwarded == 0U,
             "trame d'ID 0x120 (inconnue) -> %u message(s) transmis (attendu 0, elle est lue comme 0x20 = roue FR)",
             (unsigned)forwarded);

    unsigned idBits = (unsigned)(sizeof(((dataCan *)0)->id) * 8U);
    vt_check("B6b", "-", idBits >= 11U, "dataCan.id sur %u bits (attendu >= 11 pour un ID standard)", idBits);

    unsigned dataBytes = (unsigned)sizeof(((dataCan *)0)->data);
    vt_check("B6c", "T 11.9.2.d", dataBytes >= 8U,
             "dataCan transporte %u octets et pas de DLC (attendu 8 octets + DLC verifie)", dataBytes);
}

static void TestCanRx_Overload(void)
{
    // TRVP en retard : sa queue est pleine quand de nouvelles valeurs APPS arrivent
    Setup(100U);
    dataMessage_t old = {.id = wheelFRId, .data = 1, .timestamp = 50, .timeStatus = STATUS_TIME_OK};
    while (xQueueSend(xTRVPQueue, &old, 0) == pdPASS)
    {
    }
    (void)PushFrame(0x30, 1000);
    (void)PushFrame(0x30, 2000);
    (void)PushFrame(0x30, 3000);
    RunCanRx();

    bool latestAvailable = false;
    dataMessage_t msg = {0};
    while (xQueueReceive(xTRVPQueue, &msg, 0) == pdPASS)
    {
        if ((msg.id == apps1Id) && (msg.data == 3000))
        {
            latestAvailable = true;
        }
    }
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
