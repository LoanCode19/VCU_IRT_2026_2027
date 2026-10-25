// Secteur : demarrage du VCU (MainApp)
// Code teste : MainApp() (src/VCU.c) avec un faux xTaskCreate() qui enregistre les taches
// Problemes : B5, B8 de AUDIT_VCU.md

#include <stdbool.h>
#include <string.h>

#include "structs.h"
#include "queueHandles.h"
#include "TaskCanRX.h"
#include "TaskCanTx.h"
#include "TaskMain.h"
#include "TaskRawValuesProcessing.h"
#include "TaskIHM.h"
#include "TaskDigitalSignals.h"
#include "TaskCoolingSystem.h"
#include "test_app.h"
#include "vcu_test.h"

static bool IsCreated(TaskFunction_t task)
{
    for (unsigned i = 0; i < uxTestCreatedTaskCount(); i++)
    {
        if (pxTestCreatedTask(i)->function == task)
        {
            return true;
        }
    }
    return false;
}

static void TestIntegration_Queues(void)
{
    BootMainApp();
    bool allQueues = (xMainQueue != NULL) && (xTRVPQueue != NULL) && (xIHMQueue != NULL) &&
                     (xCanTxQueue != NULL) && (xCanRxQueue != NULL) && (xWDGQueue != NULL) && (xDSQueue != NULL);
    vt_check("NR-1", "-", allQueues, "MainApp() cree les 7 queues");
}

static void TestIntegration_TaskParameters(void)
{
    BootMainApp();
    unsigned onStack = 0;
    for (unsigned i = 0; i < uxTestCreatedTaskCount(); i++)
    {
        const TestCreatedTask_t *task = pxTestCreatedTask(i);
        if (task->parametersOnStack)
        {
            onStack++;
            vt_note("tache \"%s\" : parametres dans la pile de MainApp()", task->name);
        }
    }
    vt_check("B5a", "-", onStack == 0,
             "%u tache(s) recoivent un pointeur vers une variable locale de MainApp() (attendu 0, sinon "
             "ecrase sur Cortex-M au demarrage du scheduler)",
             onStack);
}

static void TestIntegration_AllTasksCreated(void)
{
    static const struct
    {
        TaskFunction_t task;
        const char *name;
    } expected[] = {
        {TaskCanRx, "TaskCanRx"},
        {TaskRawValuesProcessing, "TaskRawValuesProcessing"},
        {TaskMain, "TaskMain"},
        {TaskCanTx, "TaskCanTx"},
        {TaskIHM, "TaskIHM"},
        {TaskDigitalSignals, "TaskDigitalSignals"},
        {TaskCoolingSystem, "TaskCoolingSystem"}};

    BootMainApp();
    char missing[200] = "";
    unsigned missingCount = 0;
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        if (!IsCreated(expected[i].task))
        {
            missingCount++;
            strncat(missing, " ", sizeof(missing) - strlen(missing) - 1);
            strncat(missing, expected[i].name, sizeof(missing) - strlen(missing) - 1);
        }
    }
    vt_check("B8a", "-", missingCount == 0, "%u tache(s) creee(s), %u manquante(s) :%s",
             uxTestCreatedTaskCount(), missingCount, missingCount ? missing : " aucune");
}

int main(void)
{
    volatile int stackTop = 0;
    vTestSetStackTop((const void *)&stackTop);

    vt_begin("Demarrage (MainApp)");

    TestIntegration_Queues();
    TestIntegration_TaskParameters();
    TestIntegration_AllTasksCreated();

    return vt_end();
}
