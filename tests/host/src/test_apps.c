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

    // APPS2 seul au-dessus de sa plage, APPS1 a fond : bornes a 100 % tous les deux, seule la plage les distingue
    torque = Command(Apps(0, 100.0f, STATUS_TIME_OK), AppsRaw(1, potmax[1] + 300, STATUS_TIME_OK), 0, false);
    vt_check("S1p", "T 11.9.2.c", torque <= 0.0f,
             "APPS1 a 100 %%, APPS2 a %d (au-dessus de sa plage) -> couple %.1f %% (attendu 0)", potmax[1] + 300, torque);

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

    // Deux incoherences courtes separees par un retour a la normale : chacune compte ses 100 ms depuis son debut
    monitor = (AppsMonitor_t){0};
    (void)AppsMonitorStep(&monitor, true, 40.0f, 1000U);
    (void)AppsMonitorStep(&monitor, true, 40.0f, 1050U);
    (void)AppsMonitorStep(&monitor, false, 40.0f, 1060U);
    (void)AppsMonitorStep(&monitor, true, 40.0f, 2000U);
    (void)AppsMonitorStep(&monitor, true, 40.0f, 2050U);
    bool afterSecond = AppsMonitorStep(&monitor, false, 40.0f, 2060U);
    vt_check("S1q", "T 11.8.8", afterSecond && !monitor.latched,
             "incoherence de 50 ms, retour a la normale, nouvelle incoherence de 50 ms 1 s plus tard -> couple %s, "
             "faute %s (attendu autorise, non confirmee : chaque incoherence compte depuis son debut)",
             afterSecond ? "autorise" : "coupe", monitor.latched ? "confirmee" : "non confirmee");
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

    // APPS2 monte a l'envers, les deux APPS a 40 % : coherents, le couple suit la pedale
    float inverted = Command(Apps(0, 40.0f, STATUS_TIME_OK), Apps(1, 40.0f, STATUS_TIME_OK), 0, false);
    vt_check("S4c", "T 11.8.6", inverted > 30.0f,
             "APPS2 monte a l'envers (%d -> %d), les deux APPS a 40 %% -> couple %.1f %% (attendu ~39)", potmin[1],
             potmax[1], inverted);

    potmin[1] = savedMin;
    potmax[1] = savedMax;

    // Calibration degeneree (course nulle) ou index de capteur invalide : 0 %, jamais une division par zero
    int savedMax0 = potmax[0];
    potmax[0] = potmin[0];
    float flat = NormalizeThrottle(potmin[0], 0);
    potmax[0] = savedMax0;
    float badIndex = NormalizeThrottle(2000, 2);
    vt_check("S4d", "-", (flat == 0.0f) && (badIndex == 0.0f),
             "course nulle (potmin = potmax) -> %.1f %% ; capteur n. 2 (inexistant) -> %.1f %% (attendu 0 et 0)", flat,
             badIndex);
}

// Fenetre de plage : la course, elargie d'une marge de chaque cote. Dans la marge, la valeur est bornee a la course.
static void TestApps_RangeWindow(void)
{
    dataMessage_t below = AppsRaw(0, potmin[0] - 300, STATUS_TIME_OK);
    dataMessage_t zero = Apps(1, 0.0f, STATUS_TIME_OK);
    bool belowFault = AppsImplausible(&below, &zero);
    vt_check("S1r", "T 11.9.2.c", belowFault,
             "APPS1 a %d (300 sous sa course), APPS2 a 0 %% -> incoherence %s (attendu signalee)", potmin[0] - 300,
             belowFault ? "signalee" : "non signalee");

    ResetSpeedFilter();
    float over = Command(AppsRaw(0, potmax[0] + 50, STATUS_TIME_OK), AppsRaw(1, potmax[1] + 50, STATUS_TIME_OK), 0,
                         false);
    vt_check("S1s", "-", (over > 99.0f) && (over <= 100.0f),
             "les deux APPS 50 points au-dessus de leur course -> couple %.1f %% (attendu 100 : tolere, borne a la "
             "course)",
             over);

    ResetSpeedFilter();
    dataMessage_t under = AppsRaw(0, potmin[0] - 50, STATUS_TIME_OK);
    dataMessage_t nine = Apps(1, 9.0f, STATUS_TIME_OK);
    bool underFault = AppsImplausible(&under, &nine);
    vt_check("S1t", "-", !underFault,
             "APPS1 50 points sous sa course (compte 0 %%), APPS2 a 9 %% -> incoherence %s (attendu non signalee : 9 "
             "points d'ecart)",
             underFault ? "signalee" : "non signalee");
}

static void TestApps_PedalReleased(void)
{
    float torque;

    torque = Command(Apps(0, 0.0f, STATUS_TIME_OK), Apps(1, 0.0f, STATUS_TIME_OK), 0, false);
    vt_check("S6a", "T 11.8.12", torque <= 0.0f, "pedale relachee -> couple %.1f %% (attendu <= 0)", torque);

    torque = Command(Apps(0, 1.9f, STATUS_TIME_OK), Apps(1, 1.9f, STATUS_TIME_OK), 0, false);
    vt_check("S6b", "T 11.8.12", torque <= 0.0f,
             "pedale a 1,9 %% (zone morte de 2 %%) -> couple %.1f %% (attendu 0)", torque);

    // Dans la zone morte, la demande vaut exactement 0, jamais un petit nombre negatif
    float dead = CalcThrottle(throtdead - 0.1f, 0, false);
    vt_check("S6c", "T 11.8.12", dead == 0.0f,
             "pedale a %.1f %% (zone morte de %.0f %%) -> demande %.3f %% (attendu 0 exactement)", throtdead - 0.1f,
             throtdead, dead);

    // Au-dela, la course restante est remise sur 0..100 % : 51 % -> (51 - 2) x 100 / 98 = 50 %
    float scaled = CalcThrottle(51.0f, 0, false);
    float scaledExpected = (51.0f - throtdead) * 100.0f / (100.0f - throtdead);
    vt_check("S6d", "-", (scaled > scaledExpected - 0.01f) && (scaled < scaledExpected + 0.01f),
             "pedale a 51 %% -> demande %.2f %% (attendu %.2f : zone morte de %.0f %% puis remise a l'echelle)", scaled,
             scaledExpected, throtdead);
}

// Verification frein/accelerateur : plus exigee par FS 2026 (le BSPD materiel couvre ce cas),
// mais recommandee pour ne pas declencher le BSPD (T10, decide le 27/10/2026 : verrou au-dessus de 25 %, relache
// sous 5 %).
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

    // S9d : sous 25 %, frein et pedale ensemble ne verrouillent rien : le couple revient des que le frein est relache
    (void)Command(Apps(0, 20.0f, STATUS_TIME_OK), Apps(1, 20.0f, STATUS_TIME_OK), 0, true);
    torque = Command(Apps(0, 20.0f, STATUS_TIME_OK), Apps(1, 20.0f, STATUS_TIME_OK), 0, false);
    vt_check("S9d", "recommande", torque > 10.0f,
             "frein + APPS 20 %% (sous 25 %%), puis frein relache -> couple %.1f %% (attendu ~18 : pas de verrou)",
             torque);
}

int main(void)
{
    vt_begin("Pedale d'accelerateur (APPS)");
    initThrottleValues();
    ResetSpeedFilter();

    TestApps_Plausibility();
    TestApps_Monitor();
    TestApps_InputNotModified();
    TestApps_RangeWindow();
    TestApps_PedalReleased();
    TestApps_BrakePlausibility();

    return vt_end();
}
