// Secteur : vitesse vehicule a partir des 4 roues
// Code teste : calculateSpeed(), WheelRawToMmS() (src/utils.c)
// Problemes : B4 de AUDIT_VCU.md, D7 (+ tests de non-regression NR)
//
// Vitesses de roue en mm/s (D7) : TRVP les convertit des la reception. Ecart tolere entre deux roues :
// WHEEL_MAX_DIFF_MMS (1 m/s).

#include "structs.h"
#include "utils.h"
#include "vcu_test.h"

static dataMessage_t Wheel(int32_t value, DataTimeStatus_t status)
{
    dataMessage_t msg = {0};
    msg.data = value;
    msg.timeStatus = status;
    return msg;
}

static void TestVitesse_InvalidWheels(void)
{
    // Roues avant perdues (valeur 0 restee en memoire), roues arriere valides
    dataMessage_t wheels[4] = {
        Wheel(0, STATUS_TIMEOUT), Wheel(0, STATUS_TIMEOUT),
        Wheel(8000, STATUS_TIME_OK), Wheel(8200, STATUS_TIME_OK)};
    int speed = calculateSpeed(wheels, 4);
    vt_check("B4a", "-", speed == 8100, "FR et FL en timeout, RR=8000, RL=8200 mm/s -> %d (attendu 8100)", speed);

    // Une roue invalide (valeur aberrante 30000) a chaque position : les 3 autres sont coherentes
    const char *names[4] = {"FR", "FL", "RR", "RL"};
    for (int invalid = 0; invalid < 4; invalid++)
    {
        int32_t valid[3] = {10000, 10200, 9900};
        dataMessage_t set[4];
        int v = 0;
        for (int i = 0; i < 4; i++)
        {
            set[i] = (i == invalid) ? Wheel(30000, STATUS_TIMEOUT) : Wheel(valid[v++], STATUS_TIME_OK);
        }
        speed = calculateSpeed(set, 4);
        char id[8] = {'B', '4', 'b', (char)('1' + invalid), '\0'};
        vt_check(id, "-", speed == 10033,
                 "roue %s invalide (30000), autres 10000/10200/9900 mm/s -> %d (attendu 10033)", names[invalid], speed);
    }
}

static void TestVitesse_NonRegression(void)
{
    dataMessage_t wheels[4] = {
        Wheel(10000, STATUS_TIME_OK), Wheel(10200, STATUS_TIME_OK),
        Wheel(9900, STATUS_TIME_OK), Wheel(30000, STATUS_TIME_OK)};
    int speed = calculateSpeed(wheels, 4);
    vt_check("NR-1", "-", speed == 10100,
             "4 roues valides (une aberrante) -> %d mm/s (attendu 10100, moyenne des 2 du milieu)", speed);

    dataMessage_t twoFar[4] = {
        Wheel(12000, STATUS_TIME_OK), Wheel(14000, STATUS_TIME_OK),
        Wheel(0, STATUS_TIMEOUT), Wheel(0, STATUS_TIMEOUT)};
    speed = calculateSpeed(twoFar, 4);
    vt_check("NR-2", "-", speed == 0, "2 roues valides incoherentes (12000 / 14000 mm/s) -> %d (attendu 0)", speed);

    dataMessage_t none[4] = {
        Wheel(5000, STATUS_NOT_READY), Wheel(5000, STATUS_TIMEOUT),
        Wheel(5000, STATUS_TIMEOUT), Wheel(5000, STATUS_NOT_READY)};
    speed = calculateSpeed(none, 4);
    vt_check("NR-3", "-", speed == 0, "aucune roue valide -> %d (attendu 0)", speed);
}

// D7 : conversion d'une vitesse de roue brute en mm/s, avec les constantes de project_config.h
static void TestVitesse_Conversion(void)
{
#ifdef WHEEL_SPEED_PROVISIONAL
    vt_blocked("D7a", "-",
               "rayon de roue (%.2f m) et type de capteur (tr/min) provisoires : a mesurer sur la voiture, puis "
               "retirer WHEEL_SPEED_PROVISIONAL (project_config.h)",
               WHEEL_RADIUS_M);
#else
    vt_check("D7a", "-", true, "constantes de roue mesurees : rayon %.3f m", WHEEL_RADIUS_M);
#endif

#if WHEEL_SENSOR_RAW_IS_RPM
    // 600 tr/min de roue : v = 600 x 2 pi x r / 60 = 20 pi r
    int32_t speed = WheelRawToMmS(600);
    int32_t expected = (int32_t)(20.0 * 3.14159265358979 * (double)WHEEL_RADIUS_M * 1000.0 + 0.5);
    vt_check("D7b", "-", (speed >= expected - 1) && (speed <= expected + 1),
             "roue a 600 tr/min, rayon %.2f m -> %ld mm/s (attendu %ld)", WHEEL_RADIUS_M, (long)speed, (long)expected);
#endif
}

int main(void)
{
    vt_begin("Vitesse vehicule");

    TestVitesse_InvalidWheels();
    TestVitesse_NonRegression();
    TestVitesse_Conversion();

    return vt_end();
}
