#ifndef TEST_HOST_TEST_CAN_H
#define TEST_HOST_TEST_CAN_H

// Faux CanTx_Send() du banc (src/can_stub.c) : les trames emises par le VCU sont enregistrees
// au lieu de partir sur un bus.

#include <stdbool.h>
#include <stdint.h>

#include "structs.h"

void vTestCanReset(void);                                // oublie les trames, emission acceptee
void vTestCanSetTxFails(bool fails);                     // true : CanTx_Send() refuse (boites pleines)
unsigned uxTestCanTxCount(void);                         // trames emises depuis vTestCanReset()
bool xTestCanLastTx(uint32_t canId, CanFrame_t *frame); // derniere trame emise avec cet ID

#endif // TEST_HOST_TEST_CAN_H
