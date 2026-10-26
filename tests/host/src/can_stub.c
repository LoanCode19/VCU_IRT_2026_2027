// Faux CanTx_Send() (stm32Interface.h) : sur la carte, la trame part sur le bus CAN ; dans le banc,
// elle est enregistree pour que les tests la lisent (test_can.h).

#include <stdbool.h>
#include <stdint.h>

#include "structs.h"
#include "test_can.h"

#define TEST_CAN_MAX_TX 32U

static CanFrame_t sTxFrames[TEST_CAN_MAX_TX];
static unsigned sTxCount = 0U;
static bool sTxFails = false;

bool CanTx_Send(const CanFrame_t *frame)
{
    if (sTxFails)
    {
        return false;
    }
    if (sTxCount < TEST_CAN_MAX_TX)
    {
        sTxFrames[sTxCount] = *frame;
    }
    sTxCount++;
    return true;
}

void vTestCanReset(void)
{
    sTxCount = 0U;
    sTxFails = false;
}

void vTestCanSetTxFails(bool fails)
{
    sTxFails = fails;
}

unsigned uxTestCanTxCount(void)
{
    return sTxCount;
}

bool xTestCanLastTx(uint32_t canId, CanFrame_t *frame)
{
    unsigned stored = (sTxCount < TEST_CAN_MAX_TX) ? sTxCount : TEST_CAN_MAX_TX;
    for (unsigned i = stored; i > 0U; i--)
    {
        if (sTxFrames[i - 1U].id == canId)
        {
            *frame = sTxFrames[i - 1U];
            return true;
        }
    }
    return false;
}
