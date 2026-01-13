/*
 * PROC1_$BEGIN_ATOMIC_OP - Begin atomic operation region
 *
 * Increments the atomic operation depth counter. While depth > 0,
 * the dispatcher will crash if called (atomic operations must not
 * be preempted).
 *
 * Original address: 0x00e209e6
 */

#include "proc1.h"

void PROC1_$BEGIN_ATOMIC_OP(void)
{
    PROC1_$ATOMIC_OP_DEPTH++;
}
