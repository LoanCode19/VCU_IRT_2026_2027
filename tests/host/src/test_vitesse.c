// Secteur : vitesse vehicule a partir des 4 roues
// Code teste : calculateSpeed() (src/utils.c)
// Problemes : B4 de AUDIT_VCU.md (+ tests de non-regression NR)

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
        Wheel(80, STATUS_TIME_OK), Wheel(82, STATUS_TIME_OK)};
    int speed = calculateSpeed(wheels, 4);
    vt_check("B4a", "-", speed == 81, "FR et FL en timeout, RR=80, RL=82 -> %d (attendu 81)", speed);

    // Une roue invalide (valeur aberrante 3000) a chaque position : les 3 autres sont coherentes
    const char *names[4] = {"FR", "FL", "RR", "RL"};
    for (int invalid = 0; invalid < 4; invalid++)
    {
        int32_t valid[3] = {1000, 1020, 990};
        dataMessage_t set[4];
        int v = 0;
        for (int i = 0; i < 4; i++)
        {
            set[i] = (i == invalid) ? Wheel(3000, STATUS_TIMEOUT) : Wheel(valid[v++], STATUS_TIME_OK);
        }
        speed = calculateSpeed(set, 4);
        char id[8] = {'B', '4', 'b', (char)('1' + invalid), '\0'};
        vt_check(id, "-", speed == 1003,
                 "roue %s invalide (3000), autres 1000/1020/990 -> %d (attendu 1003)", names[invalid], speed);
    }
}

static void TestVitesse_NonRegression(void)
{
    dataMessage_t wheels[4] = {
        Wheel(1000, STATUS_TIME_OK), Wheel(1020, STATUS_TIME_OK),
        Wheel(990, STATUS_TIME_OK), Wheel(3000, STATUS_TIME_OK)};
    int speed = calculateSpeed(wheels, 4);
    vt_check("NR-1", "-", speed == 1010, "4 roues valides (une aberrante) -> %d (attendu 1010, moyenne des 2 du milieu)", speed);

    dataMessage_t twoFar[4] = {
        Wheel(1200, STATUS_TIME_OK), Wheel(1400, STATUS_TIME_OK),
        Wheel(0, STATUS_TIMEOUT), Wheel(0, STATUS_TIMEOUT)};
    speed = calculateSpeed(twoFar, 4);
    vt_check("NR-2", "-", speed == 0, "2 roues valides incoherentes (1200 / 1400) -> %d (attendu 0)", speed);

    dataMessage_t none[4] = {
        Wheel(500, STATUS_NOT_READY), Wheel(500, STATUS_TIMEOUT),
        Wheel(500, STATUS_TIMEOUT), Wheel(500, STATUS_NOT_READY)};
    speed = calculateSpeed(none, 4);
    vt_check("NR-3", "-", speed == 0, "aucune roue valide -> %d (attendu 0)", speed);
}

int main(void)
{
    vt_begin("Vitesse vehicule");

    TestVitesse_InvalidWheels();
    TestVitesse_NonRegression();

    return vt_end();
}
