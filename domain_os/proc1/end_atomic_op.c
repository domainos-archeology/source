/*
 * PROC1_$END_ATOMIC_OP - End atomic operation region
 *
 * Decrements the atomic operation depth counter.
 *
 * Original address: 0x00e209fa
 */

#include "proc1.h"

void PROC1_$END_ATOMIC_OP(void)
{
    PROC1_$ATOMIC_OP_DEPTH--;
}
