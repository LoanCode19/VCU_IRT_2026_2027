// Secteur : Ready-to-Drive
// Code teste : la machine a etats de src/vcuState.c (fonction pure, sans FreeRTOS)
// Problemes : S3 de AUDIT_VCU.md ; decisions T8 (revisee le 27/10/2026 : interrupteur ON/OFF), T19, T22

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "project_config.h"
#include "vcuState.h"
#include "vcu_test.h"

// Trame d'etat recue a temps, avec ces quatre valeurs
static VcuInputs_t In(int32_t tsActive, int32_t sdcClosed, int32_t brake, int32_t r2dSwitch)
{
    return VcuInputsFromSignals(true, tsActive, sdcClosed, brake, r2dSwitch);
}

static VcuOutputs_t Step(VcuStateMachine_t *sm, VcuInputs_t in, TickType_t now)
{
    return VcuStateStep(sm, &in, now);
}

// Machine neuve amenee en R2D par la sequence de T8 a t = 0 et 10 ms ; frein relache a t = 20 ms
static VcuStateMachine_t InR2D(void)
{
    VcuStateMachine_t sm;
    VcuStateInit(&sm);
    (void)Step(&sm, In(1, 1, 0, 0), 0);
    (void)Step(&sm, In(1, 1, 1, 1), 10);
    (void)Step(&sm, In(1, 1, 0, 1), 20);
    return sm;
}

static const char *Allowed(VcuOutputs_t out)
{
    return out.torqueAllowed ? "autorise" : "interdit";
}

static void TestR2D_Entry(void)
{
    VcuStateMachine_t sm;

    // S3e : l'action dediee doit se faire pendant le freinage (EV 4.11.7)
    VcuStateInit(&sm);
    (void)Step(&sm, In(1, 1, 0, 0), 0);
    VcuOutputs_t noBrake = Step(&sm, In(1, 1, 0, 1), 10);
    VcuOutputs_t brakeAfter = Step(&sm, In(1, 1, 1, 1), 20);
    vt_check("S3e", "EV 4.11.7", !noBrake.torqueAllowed && !brakeAfter.torqueAllowed,
             "interrupteur passe sur ON sans frein -> couple %s ; puis frein appuye -> couple %s (attendu interdit, "
             "interdit)",
             Allowed(noBrake), Allowed(brakeAfter));

    // S3f : interrupteur deja sur ON au demarrage (laisse sur ON au dernier roulage) : il faut le couper puis le
    // remettre sur ON
    VcuStateInit(&sm);
    VcuOutputs_t atBoot = Step(&sm, In(1, 1, 1, 1), 0);
    VcuOutputs_t later = Step(&sm, In(1, 1, 1, 1), 10);
    (void)Step(&sm, In(1, 1, 1, 0), 20);
    VcuOutputs_t again = Step(&sm, In(1, 1, 1, 1), 30);
    vt_check("S3f", "EV 4.11.7", !atBoot.torqueAllowed && !later.torqueAllowed && again.torqueAllowed,
             "interrupteur sur ON des le demarrage, frein appuye -> couple %s, puis %s ; coupe puis remis sur ON -> %s "
             "(attendu interdit, interdit, autorise)",
             Allowed(atBoot), Allowed(later), Allowed(again));

    // S3g : la sequence complete
    VcuStateInit(&sm);
    (void)Step(&sm, In(1, 1, 0, 0), 0);
    VcuOutputs_t entered = Step(&sm, In(1, 1, 1, 1), 10);
    vt_check("S3g", "EV 4.11.7", entered.torqueAllowed && entered.driveEnable && (sm.state == VCU_R2D),
             "TS actif, SDC ferme, interrupteur passe de OFF a ON pendant un freinage -> couple %s, Drive enable %d, "
             "etat %d (attendu autorise, 1, %d)",
             Allowed(entered), entered.driveEnable ? 1 : 0, (int)sm.state, (int)VCU_R2D);
}

static void TestR2D_Exit(void)
{
    // S3h : sortie immediate quand le SDC s'ouvre (EV 4.11.8) ou que le TS tombe
    VcuStateMachine_t sm = InR2D();
    VcuOutputs_t sdcOpen = Step(&sm, In(1, 0, 0, 1), 30);
    sm = InR2D();
    VcuOutputs_t tsLost = Step(&sm, In(0, 1, 0, 1), 30);
    vt_check("S3h", "EV 4.11.8",
             !sdcOpen.torqueAllowed && !sdcOpen.driveEnable && !tsLost.torqueAllowed && !tsLost.driveEnable,
             "en R2D : SDC ouvert -> couple %s, Drive enable %d ; TS perdu -> couple %s, Drive enable %d (attendu "
             "interdit et 0 dans les deux cas)",
             Allowed(sdcOpen), sdcOpen.driveEnable ? 1 : 0, Allowed(tsLost), tsLost.driveEnable ? 1 : 0);

    // S3j : trame d'etat perdue (T19, T 11.9.2.d) -> sortie ; quand elle revient, interrupteur toujours sur ON, il
    // faut le revoir sur OFF
    sm = InR2D();
    VcuInputs_t lostFrame = VcuInputsFromSignals(false, 1, 1, 0, 1);
    VcuOutputs_t lost = VcuStateStep(&sm, &lostFrame, 130);
    VcuOutputs_t back = Step(&sm, In(1, 1, 1, 1), 140);
    vt_check("S3j", "T 11.9.2.d", !lost.torqueAllowed && !lost.driveEnable && !back.torqueAllowed,
             "en R2D, trame d'etat perdue -> couple %s, Drive enable %d ; trame revenue, interrupteur sur ON et frein -> "
             "couple %s (attendu interdit, 0, interdit)",
             Allowed(lost), lost.driveEnable ? 1 : 0, Allowed(back));

    // S3k : apres une sortie, une nouvelle sequence remet en R2D (D 2.3.3 : redemarrer sans aide exterieure)
    sm = InR2D();
    (void)Step(&sm, In(1, 0, 0, 1), 30);
    VcuOutputs_t sdcClosedAgain = Step(&sm, In(1, 1, 1, 1), 40);
    (void)Step(&sm, In(1, 1, 0, 0), 50);
    VcuOutputs_t newSequence = Step(&sm, In(1, 1, 1, 1), 60);
    vt_check("S3k", "D 2.3.3", !sdcClosedAgain.torqueAllowed && newSequence.torqueAllowed,
             "sortie par le SDC ; SDC referme, interrupteur toujours sur ON -> couple %s ; interrupteur coupe puis remis "
             "sur ON pendant un freinage -> couple %s (attendu interdit, autorise)",
             Allowed(sdcClosedAgain), Allowed(newSequence));

    // S3n : couper l'interrupteur fait sortir du R2D (T8 revise) ; le remettre sur ON sans frein ne suffit pas
    sm = InR2D();
    VcuOutputs_t switchedOff = Step(&sm, In(1, 1, 0, 0), 30);
    VcuOutputs_t onWithoutBrake = Step(&sm, In(1, 1, 0, 1), 40);
    vt_check("S3n", "EV 4.11.7",
             !switchedOff.torqueAllowed && !switchedOff.driveEnable && !onWithoutBrake.torqueAllowed,
             "en R2D, interrupteur passe sur OFF -> couple %s, Drive enable %d ; remis sur ON sans frein -> couple %s "
             "(attendu interdit, 0, interdit)",
             Allowed(switchedOff), switchedOff.driveEnable ? 1 : 0, Allowed(onWithoutBrake));

    // S3o : une valeur autre que 0 ou 1 dans la trame d'etat (donnee corrompue, T 11.9.2.c) : trame invalide
    bool allRejected = true;
    for (int i = 0; i < 4; i++)
    {
        int32_t raw[4] = {1, 1, 0, 1};
        raw[i] = 2;
        if (VcuInputsFromSignals(true, raw[0], raw[1], raw[2], raw[3]).fresh)
        {
            allRejected = false;
        }
    }
    sm = InR2D();
    VcuOutputs_t corrupt = Step(&sm, In(1, 1, 0, 2), 30);
    vt_check("S3o", "T 11.9.2.c", allRejected && !corrupt.torqueAllowed,
             "valeur 2 dans un des quatre octets -> trame %s ; en R2D, interrupteur a 2 -> couple %s (attendu invalide, "
             "interdit)",
             allRejected ? "invalide" : "acceptee", Allowed(corrupt));
}

static void TestR2D_Buzzer(void)
{
    // S3i : son R2D pendant R2D_BUZZER_MS a l'entree (EV 4.12.1 : de 1 a 3 s)
    VcuStateMachine_t sm;
    VcuStateInit(&sm);
    (void)Step(&sm, In(1, 1, 0, 0), 990);
    VcuOutputs_t entry = Step(&sm, In(1, 1, 1, 1), 1000);
    VcuOutputs_t at1900 = Step(&sm, In(1, 1, 0, 1), 2900);
    VcuOutputs_t at2100 = Step(&sm, In(1, 1, 0, 1), 3100);
    vt_check("S3i", "EV 4.12.1", entry.buzzerOn && at1900.buzzerOn && !at2100.buzzerOn,
             "entree en R2D -> son %s ; 1,9 s apres -> %s ; 2,1 s apres -> %s (attendu ON, ON, OFF : %u ms, entre 1 et "
             "3 s)",
             entry.buzzerOn ? "ON" : "OFF", at1900.buzzerOn ? "ON" : "OFF", at2100.buzzerOn ? "ON" : "OFF",
             (unsigned)R2D_BUZZER_MS);
}

int main(void)
{
    vt_begin("Ready-to-Drive (machine a etats)");

    TestR2D_Entry();
    TestR2D_Exit();
    TestR2D_Buzzer();

    return vt_end();
}
