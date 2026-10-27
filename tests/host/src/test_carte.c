// Secteur : code CAN de la carte, sur PC
// Code teste : la zone USER CODE 4 de VCU_STM32F407/Core/Src/main.c (StartCan, ISR de reception,
// callback d'erreur, CanTx_Send), copiee par run_tests.py et compilee avec une fausse HAL
// (hal_stub.c), avec TaskCanTx et les queues de MainApp() en vrai
// Problemes : C1, C6 et emission vers l'onduleur. Ce sont les essais carte C-C1a et C-C6a, emules
// faute d'electronique (decision P10). Aussi : les trames de l'autotest de la carte (selftest.c).

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hal_stub.h"
#include "structs.h"
#include "canIds.h"
#include "queueHandles.h"
#include "my_fp.h"
#include "TaskCanRX.h"
#include "TaskCanTx.h"
#include "selftest.h"
#include "throttle.h"
#include "stm32Interface.h"
#include "test_app.h"
#include "vcu_test.h"

// Variables globales de main.c dont se sert la zone USER CODE 4
CAN_HandleTypeDef hcan1;
IWDG_HandleTypeDef hiwdg;

// _write() de la carte (printf vers la console SWV) : renommee pour ne pas croiser celle du PC
#define _write BoardWrite
int BoardWrite(int file, char *ptr, int len);

#include "main_user_code_4.c" // copie par run_tests.py

#define TEST_EXT_ID 0x18FF50E5UL // un ID etendu (29 bits) quelconque
#define TEST_TICK 1234U

static CanTxParam_t sCanTxParams; // static : jamais sur la pile (cf. B5)

static void Setup(void)
{
    BootMainApp();
    vHalStubReset();
    hcan1.ErrorCode = HAL_CAN_ERROR_NONE;
    memset((void *)&canRxStats, 0, sizeof(canRxStats));
    canTxErrors = 0U;
    sCanTxParams = (CanTxParam_t){&xCanTxQueue, &xWDGQueue, TaskCanTxId};
    vTestSetTickAutoIncrement(false);
    vTestSetTickCount(TEST_TICK);
}

// Trame arrivee dans le FIFO0, avec les octets first, first + 1... Comme le vrai controleur, la HAL
// remplit StdId et ExtId : le champ inutile recoit une valeur fausse, pour verifier que la carte
// choisit le bon d'apres IDE.
static void PushRx(uint32_t canId, bool extended, uint8_t dlc, uint8_t first)
{
    CAN_RxHeaderTypeDef header = {
        .StdId = extended ? 0x7FFU : canId,
        .ExtId = extended ? canId : 0x1ABCDEF0UL,
        .IDE = extended ? CAN_ID_EXT : CAN_ID_STD,
        .RTR = CAN_RTR_DATA,
        .DLC = dlc,
    };
    uint8_t data[8];
    for (unsigned i = 0; i < 8U; i++)
    {
        data[i] = (uint8_t)(first + i);
    }
    (void)xHalStubPushRx(&header, data);
}

// Trames remises au VCU par l'ISR (xCanRxQueue), dans l'ordre
static unsigned PopFrames(CanFrame_t *frames, unsigned max)
{
    unsigned n = 0U;
    while ((n < max) && (xQueueReceive(xCanRxQueue, &frames[n], 0) == pdPASS))
    {
        n++;
    }
    return n;
}

static bool SameFrame(const CanFrame_t *frame, uint32_t canId, uint8_t ide, uint8_t dlc, uint8_t first)
{
    bool same = (frame->id == canId) && (frame->ide == ide) && (frame->dlc == dlc) && (frame->rxTick == TEST_TICK);
    for (unsigned i = 0; same && (i < 8U); i++)
    {
        same = (frame->data[i] == (uint8_t)(first + i));
    }
    return same;
}

// Consigne de Main vers CanTx, dans le format que TaskMain depose (pourcentage en virgule fixe)
static void PushTorqueCommand(float percent)
{
    dataMessage_t msg = {.id = inverterTorqueCmdId, .data = FP_FROMFLT(percent), .timestamp = TEST_TICK};
    (void)xQueueSend(xCanTxQueue, &msg, 0);
}

// C-C1a emule : l'ISR lit toutes les trames du FIFO, les remet entieres au VCU, puis demande un
// changement de tache une seule fois
static void TestCarte_IsrReadsAllFrames(void)
{
    Setup();
    PushRx(CAN_ID_APPS1, false, 4U, 0x10U);
    PushRx(TEST_EXT_ID, true, 8U, 0x20U);
    PushRx(CAN_ID_DTI_TEMPERATURES, false, 8U, 0x30U);
    unsigned yields = uxTestYieldFromISRCount();

    HAL_CAN_RxFifo0MsgPendingCallback(&hcan1);

    yields = uxTestYieldFromISRCount() - yields;
    CanFrame_t frames[4];
    unsigned n = PopFrames(frames, 4U);
    bool exact = (n == 3U) && SameFrame(&frames[0], CAN_ID_APPS1, 0U, 4U, 0x10U) &&
                 SameFrame(&frames[1], TEST_EXT_ID, 1U, 8U, 0x20U) &&
                 SameFrame(&frames[2], CAN_ID_DTI_TEMPERATURES, 0U, 8U, 0x30U);
    unsigned long left = (unsigned long)HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0);
    vt_check("C1e", "-", exact && (left == 0UL) && (yields == 1U),
             "ISR de la carte, 3 trames dans le FIFO (standard, etendue, DTI) -> %u remise(s) au VCU, contenu %s, "
             "%lu restee(s) dans le FIFO, %u yield (attendu 3 trames exactes, FIFO vide, 1 yield)",
             n, exact ? "exact" : "faux", left, yields);
}

// C-C1a emule : une lecture du FIFO qui echoue, meme a chaque essai, est comptee et l'ISR rend la
// main, sans Error_Handler() (le VCU se figerait, interruptions coupees). Les trames restees dans
// le FIFO sont lues a l'interruption suivante.
static void TestCarte_IsrReadError(void)
{
    Setup();
    PushRx(CAN_ID_APPS1, false, 4U, 0x10U);
    PushRx(CAN_ID_APPS2, false, 4U, 0x20U);
    vHalStubSetRxReadFails(true);
    jmp_buf env;
    bool looped = false;
    vHalStubSetLoopEscape(&env);
    if (setjmp(env) == 0)
    {
        HAL_CAN_RxFifo0MsgPendingCallback(&hcan1);
    }
    else
    {
        looped = true;
    }
    vHalStubSetLoopEscape(NULL);
    unsigned long errors = (unsigned long)canRxStats.isrErrors;
    unsigned handlers = uxHalStubErrorHandlerCount();
    unsigned long left = (unsigned long)HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0);

    vHalStubSetRxReadFails(false);
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan1);
    CanFrame_t frames[3];
    unsigned n = PopFrames(frames, 3U);

    vt_check("C1f", "-", !looped && (errors == 1UL) && (handlers == 0U) && (left == 2UL) && (n == 2U),
             "lecture du FIFO en echec -> %s, isrErrors %lu, Error_Handler x%u, %lu trame(s) gardee(s) ; "
             "interruption suivante : %u trame(s) lue(s) (attendu : l'ISR rend la main, 1 erreur, aucun "
             "Error_Handler, 2 trames gardees puis lues)",
             looped ? "l'ISR boucle" : "l'ISR rend la main", errors, handlers, left, n);
}

// C-C6a emule : chaque bus-off est compte une fois, une autre erreur ne l'est pas. La HAL cumule les
// erreurs dans hcan->ErrorCode : sans effacer le bit, l'erreur suivante compterait un faux bus-off.
static void TestCarte_BusOffCounted(void)
{
    Setup();
    hcan1.ErrorCode = HAL_CAN_ERROR_BOF;
    HAL_CAN_ErrorCallback(&hcan1);
    unsigned long first = (unsigned long)canRxStats.busOff;

    hcan1.ErrorCode |= HAL_CAN_ERROR_EWG; // une autre erreur, sans nouveau bus-off
    HAL_CAN_ErrorCallback(&hcan1);
    unsigned long afterOther = (unsigned long)canRxStats.busOff;

    hcan1.ErrorCode |= HAL_CAN_ERROR_BOF; // deuxieme bus-off
    HAL_CAN_ErrorCallback(&hcan1);
    unsigned long second = (unsigned long)canRxStats.busOff;
    unsigned long code = (unsigned long)hcan1.ErrorCode;

    vt_check("C6c", "-", (first == 1UL) && (afterOther == 1UL) && (second == 2UL) && (code == HAL_CAN_ERROR_EWG),
             "bus-off, autre erreur, 2e bus-off -> busOff %lu, %lu, %lu ; ErrorCode 0x%lX a la fin (attendu 1, 1, 2 ; "
             "0x1 : bit du bus-off efface, autre erreur gardee)",
             first, afterOther, second, code);
}

// Le CAN demarre avec les notifications de reception (FIFO0), d'erreur et de bus-off, rien d'autre
static void TestCarte_StartCan(void)
{
    Setup();
    StartCan();
    unsigned long expected = CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_ERROR | CAN_IT_BUSOFF;
    unsigned long notifications = (unsigned long)ulHalStubNotifications();
    unsigned starts = uxHalStubStartCount();
    unsigned handlers = uxHalStubErrorHandlerCount();
    vt_check("C6d", "-", (starts == 1U) && (notifications == expected) && (handlers == 0U),
             "StartCan() -> HAL_CAN_Start x%u, notifications 0x%04lX, Error_Handler x%u (attendu 1 fois, 0x%04lX : "
             "reception FIFO0, erreur, bus-off ; aucun Error_Handler)",
             starts, notifications, handlers, expected);
}

// Consigne de 30 % : TaskCanTx -> CanTx_Send() de la carte -> HAL_CAN_AddTxMessage, en-tete du DTI
static void TestCarte_TorqueFrameToHal(void)
{
    Setup();
    PushTorqueCommand(30.0f);
    RunTaskOnce(TaskCanTx, &sCanTxParams);

    CAN_TxHeaderTypeDef header = {0};
    uint8_t data[8] = {0};
    bool sent = xHalStubLastTx(&header, data);
    bool ok = sent && (uxHalStubTxCount() == 1U) && (header.StdId == CAN_ID_DTI_SET_REL_CURRENT) &&
              (header.IDE == CAN_ID_STD) && (header.RTR == CAN_RTR_DATA) && (header.DLC == 2U) &&
              (header.TransmitGlobalTime == DISABLE) && (data[0] == 0x01U) && (data[1] == 0x2CU);
    vt_check("TX-f", "-", ok,
             "consigne de 30 %% -> HAL_CAN_AddTxMessage x%u : ID 0x%03lX, IDE %lu, RTR %lu, DLC %lu, %02X %02X "
             "(attendu 1 fois, 0x0A4, standard, donnees, DLC 2, 01 2C)",
             uxHalStubTxCount(), (unsigned long)header.StdId, (unsigned long)header.IDE, (unsigned long)header.RTR,
             (unsigned long)header.DLC, data[0], data[1]);
}

// Boites d'emission pleines : l'echec est compte. Une trame etendue garde son ID sur 29 bits.
static void TestCarte_TxRefusedAndExtended(void)
{
    Setup();
    vHalStubSetTxFails(true);
    PushTorqueCommand(30.0f);
    RunTaskOnce(TaskCanTx, &sCanTxParams);
    unsigned long errors = (unsigned long)canTxErrors;

    vHalStubSetTxFails(false);
    CanFrame_t extended = {.id = TEST_EXT_ID, .ide = 1U, .dlc = 1U, .data = {0x55U}};
    bool accepted = CanTx_Send(&extended);
    CAN_TxHeaderTypeDef header = {0};
    uint8_t data[8] = {0};
    (void)xHalStubLastTx(&header, data);
    bool ok = (errors == 1UL) && accepted && (header.IDE == CAN_ID_EXT) && (header.ExtId == TEST_EXT_ID) &&
              (header.StdId == 0U) && (header.DLC == 1U) && (data[0] == 0x55U);
    vt_check("TX-g", "-", ok,
             "emission refusee par la HAL -> canTxErrors %lu ; trame etendue -> IDE %lu, ExtId 0x%08lX, StdId 0x%lX "
             "(attendu 1 ; etendue, 0x18FF50E5, 0)",
             errors, (unsigned long)header.IDE, (unsigned long)header.ExtId, (unsigned long)header.StdId);
}

static unsigned DrainMessages(QueueHandle_t queue, dataMessage_t *messages, unsigned max)
{
    unsigned n = 0U;
    while ((n < max) && (xQueueReceive(queue, &messages[n], 0) == pdPASS))
    {
        n++;
    }
    return n;
}

static bool HasValue(const dataMessage_t *messages, unsigned count, MessageIds id, int32_t value)
{
    for (unsigned i = 0; i < count; i++)
    {
        if ((messages[i].id == id) && (messages[i].data == value))
        {
            return true;
        }
    }
    return false;
}

// Trames de l'autotest de la carte (modes 2 et 3) : le VCU doit les comprendre, sinon l'essai sur la
// carte ne prouverait rien
static void TestCarte_SelfTestFrames(void)
{
    Setup();
    CanFrame_t frames[SELFTEST_FRAME_COUNT];
    SelfTest_BuildFrames(frames);
    for (unsigned i = 0; i < SELFTEST_FRAME_COUNT; i++)
    {
        CanRx_OnFrameFromISR(&frames[i], NULL);
    }
    static CanRxParams_t canRxParams; // static : jamais sur la pile (cf. B5)
    canRxParams = (CanRxParams_t){&xMainQueue, &xTRVPQueue, &xCanRxQueue, &xWDGQueue, TaskCanRxId, &xCoolingQueue};
    RunTaskOnce(TaskCanRx, &canRxParams);

    dataMessage_t trvp[16];
    dataMessage_t main[16];
    unsigned nTrvp = DrainMessages(xTRVPQueue, trvp, 16U);
    unsigned nMain = DrainMessages(xMainQueue, main, 16U);
    int32_t apps1 = (int32_t)(potmin[0] + (potmax[0] - potmin[0]) * 0.4f);
    int32_t apps2 = (int32_t)(potmin[1] + (potmax[1] - potmin[1]) * 0.4f);
    bool sensors = (nTrvp == 6U) && HasValue(trvp, nTrvp, apps1Id, apps1) && HasValue(trvp, nTrvp, apps2Id, apps2) &&
                   HasValue(trvp, nTrvp, wheelFRId, 0) && HasValue(trvp, nTrvp, wheelFLId, 0) &&
                   HasValue(trvp, nTrvp, wheelRRId, 0) && HasValue(trvp, nTrvp, wheelRLId, 0);
    bool inverter = (nMain == 3U) && HasValue(main, nMain, inverter_tempId, 40) &&
                    HasValue(main, nMain, motor_tempId, 40) && HasValue(main, nMain, voltageId, 350);
    unsigned long rejected = (unsigned long)(canRxStats.unknownId + canRxStats.badDlc);
    vt_check("AT-a", "-", sensors && inverter && (rejected == 0UL),
             "trames de l'autotest de la carte -> %u message(s) vers TRVP (%s), %u vers Main (%s), %lu rejetee(s) "
             "(attendu 6 : APPS a 40 %%, roues a 0 ; 3 : 40 degres, 40 degres, 350 V ; aucun rejet)",
             nTrvp, sensors ? "bons" : "faux", nMain, inverter ? "bons" : "faux", rejected);
}

int main(void)
{
    vt_begin("Code CAN de la carte (fausse HAL)");

    TestCarte_IsrReadsAllFrames();
    TestCarte_IsrReadError();
    TestCarte_BusOffCounted();
    TestCarte_StartCan();
    TestCarte_TorqueFrameToHal();
    TestCarte_TxRefusedAndExtended();
    TestCarte_SelfTestFrames();

    return vt_end();
}
