// Secteur : vitesse vehicule a partir des 4 roues
// Code teste : calculateSpeed(), WheelRawToMmS() (src/utils.c)
// Problemes : B4 de AUDIT_VCU.md, D7 (+ tests de non-regression NR)
//
// Vitesses de roue en mm/s (D7) : TRVP les convertit des la reception. Ecart tolere entre deux roues :
// WHEEL_MAX_DIFF_MMS au plus (T18 revise le 01/11/2026 : 2 m/s, a confirmer avec l'equipe). Les valeurs des tests
// suivent la constante.

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

    // Deux fois l'ecart tolere : 12000 / 14000 mm/s avec l'ancien 1 m/s, 12000 / 16000 avec 2 m/s (T18)
    const int32_t farValue = 12000 + 2 * WHEEL_MAX_DIFF_MMS;
    dataMessage_t twoFar[4] = {
        Wheel(12000, STATUS_TIME_OK), Wheel(farValue, STATUS_TIME_OK),
        Wheel(0, STATUS_TIMEOUT), Wheel(0, STATUS_TIMEOUT)};
    speed = calculateSpeed(twoFar, 4);
    vt_check("NR-2", "-", speed == 0, "2 roues valides incoherentes (12000 / %ld mm/s) -> %d (attendu 0)",
             (long)farValue, speed);

    // T18 (revise le 01/11/2026, revisable) : un ecart egal a WHEEL_MAX_DIFF_MMS est accepte, avec deux roues comme
    // avec trois
    dataMessage_t twoAtLimit[4] = {
        Wheel(12000, STATUS_TIME_OK), Wheel(12000 + WHEEL_MAX_DIFF_MMS, STATUS_TIME_OK),
        Wheel(0, STATUS_TIMEOUT), Wheel(0, STATUS_TIMEOUT)};
    speed = calculateSpeed(twoAtLimit, 4);
    vt_check("B4k", "-", speed == 12000 + WHEEL_MAX_DIFF_MMS / 2,
             "2 roues valides a %d mm/s d'ecart (la limite) -> %d (attendu %d : moyennees, l'ecart est accepte au plus)",
             WHEEL_MAX_DIFF_MMS, speed, 12000 + WHEEL_MAX_DIFF_MMS / 2);
    dataMessage_t threeAtLimit[4] = {
        Wheel(10000, STATUS_TIME_OK), Wheel(10000 + WHEEL_MAX_DIFF_MMS, STATUS_TIME_OK),
        Wheel(10000 + 2 * WHEEL_MAX_DIFF_MMS, STATUS_TIME_OK), Wheel(0, STATUS_TIMEOUT)};
    speed = calculateSpeed(threeAtLimit, 4);
    vt_check("B4l", "-", speed == 10000 + WHEEL_MAX_DIFF_MMS,
             "3 roues valides a %d mm/s d'ecart l'une de l'autre (la limite) -> %d (attendu %d : moyenne des trois, "
             "comme avec deux roues)",
             WHEEL_MAX_DIFF_MMS, speed, 10000 + WHEEL_MAX_DIFF_MMS);

    // Trois roues valides : celle qui s'ecarte de plus de WHEEL_MAX_DIFF_MMS est laissee de cote
    const struct
    {
        const char *id;
        int32_t values[3];
        int expected;
        const char *what;
    } three[] = {
        {"B4f", {10000, 10500, 20000}, 10250, "la plus rapide ecartee"},
        {"B4g", {5000, 10000, 10500}, 10250, "la plus lente ecartee"},
        {"B4h", {5000, 10000, 15000}, 0, "toutes trop differentes : vitesse inconnue"},
    };
    for (unsigned i = 0; i < sizeof(three) / sizeof(three[0]); i++)
    {
        dataMessage_t set[4] = {
            Wheel(three[i].values[0], STATUS_TIME_OK), Wheel(0, STATUS_TIMEOUT),
            Wheel(three[i].values[1], STATUS_TIME_OK), Wheel(three[i].values[2], STATUS_TIME_OK)};
        speed = calculateSpeed(set, 4);
        vt_check(three[i].id, "-", speed == three[i].expected,
                 "3 roues valides %ld / %ld / %ld mm/s -> %d (attendu %d : %s)", (long)three[i].values[0],
                 (long)three[i].values[1], (long)three[i].values[2], speed, three[i].expected, three[i].what);
    }

    // Une seule roue valide : sa vitesse
    dataMessage_t one[4] = {
        Wheel(0, STATUS_TIMEOUT), Wheel(12000, STATUS_TIME_OK), Wheel(0, STATUS_NOT_READY), Wheel(0, STATUS_TIMEOUT)};
    speed = calculateSpeed(one, 4);
    vt_check("B4i", "-", speed == 12000, "1 roue valide a 12000 mm/s -> %d (attendu 12000)", speed);

    // Trois roues valides arrivees dans le desordre : elles sont triees avant d'etre comparees. Ecarts de 6/10 et 5/10
    // de l'ecart tolere (10600 / 10000 / 9500 mm/s avec l'ancien 1 m/s) : mal triees, elles ne seraient plus d'accord.
    const int32_t high = 10000 + (6 * WHEEL_MAX_DIFF_MMS) / 10;
    const int32_t low = 10000 - (5 * WHEEL_MAX_DIFF_MMS) / 10;
    const int expectedMean = (int)((high + 10000 + low) / 3);
    dataMessage_t unordered[4] = {
        Wheel(high, STATUS_TIME_OK), Wheel(10000, STATUS_TIME_OK), Wheel(low, STATUS_TIME_OK), Wheel(0, STATUS_TIMEOUT)};
    speed = calculateSpeed(unordered, 4);
    vt_check("B4j", "-", speed == expectedMean,
             "3 roues valides dans le desordre %ld / 10000 / %ld mm/s -> %d (attendu %d : moyenne des trois)", (long)high,
             (long)low, speed, expectedMean);

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

    // Arrondi au plus proche, dans les deux sens : 1 tr/min = 2 pi r / 60 m/s
    double oneRpm = 2.0 * 3.14159265358979 * (double)WHEEL_RADIUS_M * 1000.0 / 60.0;
    int32_t up = WheelRawToMmS(1);
    int32_t down = WheelRawToMmS(-1);
    int32_t expectedUp = (int32_t)(oneRpm + 0.5);
    vt_check("D7c", "-", (up == expectedUp) && (down == -expectedUp),
             "roue a 1 et -1 tr/min (%.2f mm/s) -> %ld et %ld mm/s (attendu %ld et %ld : arrondi au plus proche)",
             oneRpm, (long)up, (long)down, (long)expectedUp, (long)-expectedUp);
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
