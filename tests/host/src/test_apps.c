// Secteur : pedale d'accelerateur (APPS) et frein
// Code teste : GetUserThrottleCommand() (src/utils.c), AppsInRange() / AppsClamp() / CalcThrottle() (src/throttle.c)
// Problemes : S1, S4, S6, S9 de AUDIT_VCU.md, et la decision D2

#include <stdbool.h>

#include "structs.h"
#include "throttle.h"
#include "utils.h"
#include "VCU.h"
#include "vcu_test.h"

#define ADC_MAX 4095 // CAN 12 bits du STM32F407

extern float SpeedFiltered; // filtre de vitesse interne a CalcThrottle (src/throttle.c)

// Valeur brute ADC d'un APPS pour un pourcentage, avec la calibration de initThrottleValues()
static int32_t RawFromPercent(int potIdx, float percent)
{
    return (int32_t)(potmin[potIdx] + (potmax[potIdx] - potmin[potIdx]) * percent / 100.0f);
}

static dataMessage_t Apps(int potIdx, float percent, DataTimeStatus_t status)
{
    dataMessage_t msg = {0};
    msg.id = (potIdx == 0) ? apps1Id : apps2Id;
    msg.data = RawFromPercent(potIdx, percent);
    msg.timeStatus = status;
    return msg;
}

static dataMessage_t AppsRaw(int potIdx, int32_t raw, DataTimeStatus_t status)
{
    dataMessage_t msg = Apps(potIdx, 0.0f, status);
    msg.data = raw;
    return msg;
}

// Les messages sont passes par copie : un test ne voit pas l'effet de bord de S4,
// sauf S4 qui le cherche explicitement.
static float Command(dataMessage_t apps1, dataMessage_t apps2, int speed, bool brake)
{
    return GetUserThrottleCommand(&apps1, &apps2, speed, brake);
}

static void ResetSpeedFilter(void)
{
    SpeedFiltered = 0.0f;
}

static void TestApps_Plausibility(void)
{
    float torque;

    torque = Command(Apps(0, 30.0f, STATUS_TIME_OK), Apps(1, 70.0f, STATUS_TIME_OK), 0, false);
    vt_check("S1a", "T 11.8.8/9", torque <= 0.0f,
             "APPS 30 %% / 70 %% (ecart 40 pts) -> couple %.1f %% (attendu 0)", torque);

    torque = Command(Apps(0, 0.0f, STATUS_TIMEOUT), Apps(1, 80.0f, STATUS_TIME_OK), 0, false);
    vt_check("S1b", "T 11.9.2.d", torque <= 0.0f,
             "APPS1 en timeout, APPS2 80 %% -> couple %.1f %% (attendu 0)", torque);

    torque = Command(Apps(0, 0.0f, STATUS_NOT_READY), Apps(1, 80.0f, STATUS_TIME_OK), 0, false);
    vt_check("S1c", "T 11.8.9", torque <= 0.0f,
             "APPS1 jamais recu, APPS2 80 %% -> couple %.1f %% (attendu 0)", torque);

    // Court-circuit au + : l'ADC lit sa valeur max. potmax (3900) + POT_SLACK (200) > 4095,
    // donc la valeur est consideree "dans la plage" et vaut 100 %.
    torque = Command(AppsRaw(0, ADC_MAX, STATUS_TIME_OK), Apps(1, 95.0f, STATUS_TIME_OK), 0, false);
    vt_check("S1d", "T 11.9.2.b", torque <= 0.0f,
             "APPS1 court-circuite au + (ADC=%d), APPS2 95 %% -> couple %.1f %% (attendu 0)", ADC_MAX, torque);

    torque = Command(AppsRaw(0, 0, STATUS_TIME_OK), Apps(1, 50.0f, STATUS_TIME_OK), 0, false);
    vt_check("S1e", "T 11.9.2.a", torque <= 0.0f,
             "APPS1 court-circuite a la masse (ADC=0), APPS2 50 %% -> couple %.1f %% (attendu 0)", torque);

    torque = Command(Apps(0, 40.0f, STATUS_TIME_OK), Apps(1, 49.0f, STATUS_TIME_OK), 0, false);
    vt_check("S1f", "T 11.8.9", torque > 30.0f,
             "APPS 40 %% / 49 %% (ecart 9 pts, tolere) -> couple %.1f %% (attendu ~39, pas de coupure)", torque);
}

// Surveillance de TRVP (AppsMonitorStep) : une incoherence de plus de 100 ms est confirmee, et le couple
// ne revient qu'avec des APPS coherents et la pedale sous 5 % (D2)
static void TestApps_Monitor(void)
{
    AppsMonitor_t monitor = {0};
    bool first = AppsMonitorStep(&monitor, true, 40.0f, 1000U);
    vt_check("S1g", "T 11.8.8", !first, "premier cycle incoherent -> couple %s (attendu coupe)",
             first ? "autorise" : "coupe");

    (void)AppsMonitorStep(&monitor, true, 40.0f, 1100U); // 100 ms tout juste
    bool after100 = AppsMonitorStep(&monitor, false, 40.0f, 1110U);
    vt_check("S1h", "T 11.8.8", after100,
             "incoherence de 100 ms, puis APPS coherents a 40 %% -> couple %s (attendu autorise : faute non confirmee)",
             after100 ? "autorise" : "coupe");

    monitor = (AppsMonitor_t){0};
    (void)AppsMonitorStep(&monitor, true, 40.0f, 1000U);
    (void)AppsMonitorStep(&monitor, true, 40.0f, 1101U);
    bool at40 = AppsMonitorStep(&monitor, false, 40.0f, 1110U);
    bool at6 = AppsMonitorStep(&monitor, false, 6.0f, 1500U);
    vt_check("S1i", "T 11.8.8", !at40 && !at6,
             "incoherence de 101 ms, puis APPS coherents a 40 %% puis 6 %% -> couple %s puis %s (attendu coupe et "
             "coupe : faute confirmee)",
             at40 ? "autorise" : "coupe", at6 ? "autorise" : "coupe");

    bool released = AppsMonitorStep(&monitor, false, 4.0f, 1600U);
    bool again = AppsMonitorStep(&monitor, false, 40.0f, 1700U);
    vt_check("S1j", "-", released && again,
             "faute confirmee, pedale a 4 %% (D2) puis 40 %% -> couple %s puis %s (attendu autorise et autorise)",
             released ? "autorise" : "coupe", again ? "autorise" : "coupe");
}

static void TestApps_InputNotModified(void)
{
    // APPS2 monte a l'envers (potmin > potmax), comme le permet le code et le suggere T 11.8.6
    int savedMin = potmin[1];
    int savedMax = potmax[1];
    potmin[1] = 3900;
    potmax[1] = 100;

    dataMessage_t apps1 = Apps(0, 0.0f, STATUS_TIMEOUT);
    dataMessage_t apps2 = AppsRaw(1, 4500, STATUS_TIME_OK); // hors plage
    float first = GetUserThrottleCommand(&apps1, &apps2, 0, false);
    float second = GetUserThrottleCommand(&apps1, &apps2, 0, false); // meme trame, cycle suivant

    vt_check("S4a", "T 11.9.2.c", (first <= 0.0f) && (second <= 0.0f),
             "APPS2 inverse hors plage, 2 cycles sans nouvelle trame -> %.1f %% puis %.1f %% (attendu 0 et 0)",
             first, second);
    vt_check("S4b", "-", apps2.data == 4500,
             "la valeur brute du capteur n'est pas modifiee par le calcul -> %d (attendu 4500)", (int)apps2.data);

    potmin[1] = savedMin;
    potmax[1] = savedMax;
}

static void TestApps_PedalReleased(void)
{
    float torque;

    torque = Command(Apps(0, 0.0f, STATUS_TIME_OK), Apps(1, 0.0f, STATUS_TIME_OK), 0, false);
    vt_check("S6a", "T 11.8.12", torque <= 0.0f, "pedale relachee -> couple %.1f %% (attendu <= 0)", torque);

    torque = Command(Apps(0, 1.9f, STATUS_TIME_OK), Apps(1, 1.9f, STATUS_TIME_OK), 0, false);
    vt_check("S6b", "T 11.8.12", torque <= 0.0f,
             "pedale a 1,9 %% (zone morte de 2 %%) -> couple %.1f %% (attendu 0)", torque);
}

// Verification frein/accelerateur : plus exigee par FS 2026 (le BSPD materiel couvre ce cas),
// mais recommandee pour ne pas declencher le BSPD.
static void TestApps_BrakePlausibility(void)
{
    float torque;

    ResetSpeedFilter();
    torque = Command(Apps(0, 30.0f, STATUS_TIME_OK), Apps(1, 30.0f, STATUS_TIME_OK), 0, true);
    vt_check("S9a", "recommande", torque <= 0.0f,
             "frein + APPS 30 %% -> couple %.1f %% (attendu 0)", torque);

    torque = Command(Apps(0, 10.0f, STATUS_TIME_OK), Apps(1, 10.0f, STATUS_TIME_OK), 0, false);
    vt_check("S9b", "recommande", torque <= 0.0f,
             "frein relache, APPS encore a 10 %% (> 5 %%) -> couple %.1f %% (attendu 0, verrouille)", torque);

    (void)Command(Apps(0, 4.0f, STATUS_TIME_OK), Apps(1, 4.0f, STATUS_TIME_OK), 0, false);
    torque = Command(Apps(0, 20.0f, STATUS_TIME_OK), Apps(1, 20.0f, STATUS_TIME_OK), 0, false);
    vt_check("S9c", "recommande", torque > 10.0f,
             "APPS repasse sous 5 %% puis 20 %% -> couple %.1f %% (attendu ~18, deverrouille)", torque);
}

int main(void)
{
    vt_begin("Pedale d'accelerateur (APPS)");
    initThrottleValues();
    ResetSpeedFilter();

    TestApps_Plausibility();
    TestApps_Monitor();
    TestApps_InputNotModified();
    TestApps_PedalReleased();
    TestApps_BrakePlausibility();

    return vt_end();
}
