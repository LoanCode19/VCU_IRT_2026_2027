// Secteur : IHM et sorties tout-ou-rien (buzzer R2D, voyant defaut, LED de vie)
// Code teste : TaskIHM (src/TaskIHM.c) avec les queues de MainApp()
// Problemes : B7 de AUDIT_VCU.md

#include <stdbool.h>

#include "structs.h"
#include "queueHandles.h"
#include "TaskIHM.h"
#include "test_app.h"
#include "vcu_test.h"

static TaskIHMParam_t sIhmParams;

// Envoie un ordre a TaskIHM et renvoie l'ordre recu par TaskDigitalSignals
static bool Forward(Order_t order, bool state, DigitalOrder_t *out)
{
    BootMainApp();
    sIhmParams = (TaskIHMParam_t){&xIHMQueue, &xDSQueue, &xWDGQueue, TaskIHMId};
    IHMOrder_t in = {.order = order, .state = state};
    (void)xQueueSend(xIHMQueue, &in, 0);
    RunTaskOnce(TaskIHM, &sIhmParams);
    return xQueueReceive(xDSQueue, out, 0) == pdPASS;
}

static void CheckForward(const char *id, const char *rule, Order_t order, const char *name)
{
    DigitalOrder_t out = {0};
    bool received = Forward(order, true, &out);
    vt_check(id, rule, received && (out.order == order) && out.state,
             "ordre IHM %s ON -> sortie recoit order=%d state=%d (attendu order=%d state=1)",
             name, received ? (int)out.order : -1, received ? (int)out.state : -1, (int)order);
}

int main(void)
{
    vt_begin("IHM et sorties TOR");

    CheckForward("B7a", "EV 4.12.1", R2Dbuzzer, "R2Dbuzzer");
    CheckForward("B7b", "T 11.9.5", FaultLight, "FaultLight");
    CheckForward("B7c", "-", HBLed, "HBLed");

    return vt_end();
}
