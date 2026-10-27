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

// Appels repetes pour sortir de la rampe et du filtre de SpeedLimitCommand.
static float SettledProcess(float request, int speed, int motorTemp, int inverterTemp, float udc)
{
    float out = 0.0f;
    for (int i = 0; i < 100; i++)
    {
        out = ProcessThrottle(request, speed, motorTemp, inverterTemp, udc);
    }
    return out;
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

static void TestCouple_SpeedLimiter(void)
{
    float torque;

    // SpeedLimitCommand filtre la vitesse (IIR) : appels repetes a vitesse constante.
    torque = 100.0f;
    for (int i = 0; i < 100; i++)
    {
        torque = 100.0f;
        SpeedLimitCommand(&torque, 0);
    }
    vt_check("B3a", "-", torque >= 99.0f,
             "limiteur, vitesse 0, consigne 100 %% -> %.1f %% (attendu 100)", torque);

    for (int i = 0; i < 100; i++)
    {
        torque = 100.0f;
        SpeedLimitCommand(&torque, speedLimit / 2);
    }
    vt_check("B3b", "-", torque >= 99.0f,
             "limiteur, vitesse %d = moitie de speedLimit -> %.1f %% (attendu 100)", speedLimit / 2, torque);

    for (int i = 0; i < 100; i++)
    {
        torque = 100.0f;
        SpeedLimitCommand(&torque, speedLimit);
    }
    // IIRFILTER entier : (n + 15*l) >> 4 tronque, la vitesse filtree reste jusqu'a 15 sous la vraie
    vt_check("B3c", "-", torque <= 0.0f,
             "limiteur, vitesse = speedLimit (%d) -> %.1f %% (attendu 0 ; le filtre IIR entier reste sous la vitesse reelle)",
             speedLimit, torque);
}

static void TestCouple_Ramp(void)
{
    ResetState();
    float first = ProcessThrottle(30.0f, 0, NOMINAL_TEMP, NOMINAL_TEMP, NOMINAL_UDC);
    float second = ProcessThrottle(30.0f, 0, NOMINAL_TEMP, NOMINAL_TEMP, NOMINAL_UDC);
    vt_check("R1a", "-", second == first,
             "2 appels sans que le temps avance -> %.1f %% puis %.1f %% (attendu identiques : rampe en %%/s, pas par appel)",
             first, second);
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
