#include "vcuState.h"
#include "project_config.h"

static bool IsBinary(int32_t raw)
{
    return (raw == 0) || (raw == 1);
}

VcuInputs_t VcuInputsFromSignals(bool fresh, int32_t tsActive, int32_t sdcClosed, int32_t brake, int32_t r2dSwitch)
{
    VcuInputs_t in = {0};
    in.fresh = fresh && IsBinary(tsActive) && IsBinary(sdcClosed) && IsBinary(brake) && IsBinary(r2dSwitch);
    if (in.fresh)
    {
        in.tsActive = (tsActive == 1);
        in.sdcClosed = (sdcClosed == 1);
        in.brakePressed = (brake == 1);
        in.r2dSwitch = (r2dSwitch == 1);
    }
    return in;
}

void VcuStateInit(VcuStateMachine_t *sm)
{
    sm->state = VCU_TS_OFF;
    sm->lastSwitch = true; // un interrupteur deja sur ON au demarrage ne suffit pas : il faut le voir sur OFF (T8)
    sm->r2dSince = 0;
}

VcuOutputs_t VcuStateStep(VcuStateMachine_t *sm, const VcuInputs_t *in, TickType_t now)
{
    bool tsReady = in->fresh && in->tsActive && in->sdcClosed;
    bool switchOn = in->fresh && in->r2dSwitch;
    bool switchedOn = switchOn && !sm->lastSwitch; // passage OFF -> ON
    // Trame perdue : position inconnue, il faudra revoir l'interrupteur sur OFF avant un nouveau passage sur ON
    sm->lastSwitch = in->fresh ? switchOn : true;

    if (!tsReady)
    {
        sm->state = VCU_TS_OFF; // SDC ouvert, TS perdu ou trame perdue : sortie immediate (EV 4.11.8)
    }
    else if (sm->state == VCU_TS_OFF)
    {
        sm->state = VCU_TS_ON;
    }

    if ((sm->state == VCU_R2D) && !switchOn)
    {
        sm->state = VCU_TS_ON; // interrupteur coupe (T8 revise)
    }
    else if ((sm->state == VCU_TS_ON) && switchedOn && in->brakePressed)
    {
        sm->state = VCU_R2D; // action dediee pendant le freinage (EV 4.11.7)
        sm->r2dSince = now;
    }

    VcuOutputs_t out = {0};
    out.torqueAllowed = (sm->state == VCU_R2D);
    out.driveEnable = out.torqueAllowed;
    out.buzzerOn = out.torqueAllowed && ((TickType_t)(now - sm->r2dSince) < pdMS_TO_TICKS(R2D_BUZZER_MS));
    return out;
}
