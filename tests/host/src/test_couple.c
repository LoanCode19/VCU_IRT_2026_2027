// Secteur : calcul de la consigne de couple
// Code teste : CalcThrottle(), SpeedLimitCommand(), TemperatureDerate(), RampThrottle() (src/throttle.c)
//              ProcessThrottle() (src/utils.c)
// Problemes : S2, S7, S8, B3, R1 de AUDIT_VCU.md (+ tests de non-regression NR)

#include <stdbool.h>

#include "structs.h"
#include "throttle.h"
#include "utils.h"
#include "VCU.h"
#include "vcu_test.h"

#define NOMINAL_TEMP 40      // degC, sous motorTempMax / inverterTempMax
#define NOMINAL_UDC 350.0f   // V, entre udcmin (250) et udcmax (410)

extern float SpeedFiltered;  // filtre de vitesse interne a CalcThrottle
extern float throttleRamped; // etat de la rampe dans RampThrottle

// CalcThrottle limite la variation de vitesse a ThrotRpmFilt par appel : on la fait converger.
static void SettleSpeed(int speed)
{
    for (int i = 0; i < 200; i++)
    {
        (void)CalcThrottle(0.0f, speed, false);
    }
}

// Un appel de ProcessThrottle(), elapsedMs apres le precedent
static float Process(float request, float speed, int motorTemp, int inverterTemp, float udc, uint32_t elapsedMs)
{
    return ProcessThrottle(request, (int)speed, motorTemp, inverterTemp, udc, elapsedMs);
}

// Appels repetes, toutes les 10 ms, pour sortir de la rampe et du filtre de SpeedLimitCommand.
static float SettledProcess(float request, int speed, int motorTemp, int inverterTemp, float udc)
{
    float out = 0.0f;
    for (int i = 0; i < 100; i++)
    {
        out = Process(request, (float)speed, motorTemp, inverterTemp, udc, 10U);
    }
    return out;
}

// Pente de la rampe de montee a basse vitesse, en %/s
static float RampPerS(void)
{
    return throtRampPerS;
}

static void ResetState(void)
{
    initThrottleValues();
    SpeedFiltered = 0.0f;
    throttleRamped = 0.0f;
}

static void TestCouple_Brake(void)
{
    float torque;

    ResetState();
    SettleSpeed(1500);
    torque = CalcThrottle(0.0f, 1500, true);
    vt_check("S2a", "T 6.1.13", torque <= 0.0f,
             "frein appuye, 1500, pedale 0 %% -> couple %+.1f %% (attendu <= 0)", torque);

    torque = CalcThrottle(60.0f, 1500, true);
    vt_check("S2b", "T 6.1.13", torque <= 0.0f,
             "frein appuye, 1500, pedale 60 %% -> couple %+.1f %% (attendu <= 0)", torque);

    SettleSpeed(150);
    torque = CalcThrottle(0.0f, 150, true);
    vt_check("NR-1", "-", torque == 0.0f,
             "frein appuye a faible vitesse (150) -> couple %+.1f %% (attendu 0)", torque);
}

static void TestCouple_NoReverse(void)
{
    ResetState();
    float torque = SettledProcess(-50.0f, 0, NOMINAL_TEMP, NOMINAL_TEMP, NOMINAL_UDC);
    vt_check("S7a", "EV 2.2.4", torque >= 0.0f,
             "consigne -50 %% a l'arret -> couple %+.1f %% (attendu >= 0, pas de marche arriere)", torque);
}

static void TestCouple_ElectricalLimits(void)
{
    ResetState();
    float torque = SettledProcess(30.0f, 0, NOMINAL_TEMP, NOMINAL_TEMP, 200.0f);
    vt_check("S8a", "EV 2.2", torque < 30.0f,
             "consigne 30 %%, Udc 200 V (< udcmin %.0f V) -> couple %.1f %% (attendu reduit)", udcmin, torque);

    vt_blocked("S8b", "EV 2.2.1/2",
               "limite 80 kW / 500 A : ProcessThrottle() ne recoit pas le courant Idc, test a ecrire avec la future limitation");
}

// Vitesse limite, dans l'unite de SpeedLimitCommand()
static float SpeedLimitValue(void)
{
    return (float)speedLimit;
}

// Consigne de 100 % apres le limiteur, a vitesse constante (appels repetes : filtre IIR)
static float LimitAt(float speed)
{
    float torque = 100.0f;
    for (int i = 0; i < 100; i++)
    {
        torque = 100.0f;
        SpeedLimitCommand(&torque, (int)speed);
    }
    return torque;
}

static void TestCouple_SpeedLimiter(void)
{
    float limit = SpeedLimitValue();

    float torque = LimitAt(0.0f);
    vt_check("B3a", "-", torque >= 99.0f,
             "limiteur, vitesse 0, consigne 100 %% -> %.1f %% (attendu 100)", torque);

    torque = LimitAt(limit / 2.0f);
    vt_check("B3b", "-", torque >= 99.0f,
             "limiteur, vitesse %.1f = moitie de la limite -> %.1f %% (attendu 100)", limit / 2.0f, torque);

    torque = LimitAt(limit);
    // IIRFILTER entier : (n + 15*l) >> 4 tronque, la vitesse filtree reste jusqu'a 15 sous la vraie
    vt_check("B3c", "-", torque <= 0.0f,
             "limiteur, vitesse = limite (%.1f) -> %.1f %% (attendu 0 ; le filtre IIR entier reste sous la vitesse reelle)",
             limit, torque);
}

static void TestCouple_Ramp(void)
{
    ResetState();
    float first = Process(30.0f, 0.0f, NOMINAL_TEMP, NOMINAL_TEMP, NOMINAL_UDC, 10U);
    float second = Process(30.0f, 0.0f, NOMINAL_TEMP, NOMINAL_TEMP, NOMINAL_UDC, 0U); // le temps n'a pas avance
    vt_check("R1a", "-", second == first,
             "2 appels sans que le temps avance -> %.1f %% puis %.1f %% (attendu identiques : rampe en %%/s, pas par appel)",
             first, second);

    // La rampe doit avancer du temps ecoule, quel que soit le nombre d'appels
    ResetState();
    float out = 0.0f;
    for (int i = 0; i < 100; i++)
    {
        out = Process(50.0f, 0.0f, NOMINAL_TEMP, NOMINAL_TEMP, NOMINAL_UDC, 1U);
    }
    float expected = RampPerS() * 0.1f;
    vt_check("R1b", "-", (out > expected - 0.5f) && (out < expected + 0.5f),
             "consigne 50 %%, 100 appels espaces de 1 ms -> %.1f %% (attendu %.1f : %.0f %%/s pendant 100 ms)", out,
             expected, RampPerS());

    // Apres une pause (tache retardee, debogueur), la rampe n'avance que de CONTROL_MAX_ELAPSED_MS
    ResetState();
    float afterPause = Process(50.0f, 0.0f, NOMINAL_TEMP, NOMINAL_TEMP, NOMINAL_UDC, 1000U);
    float maxStep = RampPerS() * (float)CONTROL_MAX_ELAPSED_MS / 1000.0f;
    vt_check("R1c", "-", afterPause <= maxStep + 0.01f,
             "consigne 50 %%, appel 1 s apres le precedent -> %.1f %% (attendu au plus %.1f : la rampe n'avance que "
             "de %u ms apres une pause)",
             afterPause, maxStep, CONTROL_MAX_ELAPSED_MS);
}

static void TestCouple_TemperatureDerate(void)
{
    ResetState();
    float torque = SettledProcess(30.0f, 0, (int)motorTempMax + 5, NOMINAL_TEMP, NOMINAL_UDC);
    vt_check("NR-3", "-", torque <= 0.0f,
             "moteur a motorTempMax + 5 degC -> couple %.1f %% (attendu 0)", torque);

    ResetState();
    torque = SettledProcess(30.0f, 0, NOMINAL_TEMP, NOMINAL_TEMP, NOMINAL_UDC);
    vt_check("NR-4", "-", torque >= 29.0f,
             "conditions nominales, consigne 30 %% -> couple %.1f %% (attendu 30)", torque);
}

int main(void)
{
    vt_begin("Consigne de couple");

    TestCouple_Brake();
    TestCouple_NoReverse();
    TestCouple_ElectricalLimits();
    TestCouple_Ramp();
    TestCouple_TemperatureDerate();
    // En dernier : SpeedLimitCommand garde un filtre static impossible a remettre a zero
    TestCouple_SpeedLimiter();

    return vt_end();
}
