/*
 * EC_$READ - Read a level-1 eventcount's value
 *
 * Parameters:
 *   ec - Pointer to the eventcount (argument 1, (0x8,A6))
 *
 * Returns:
 *   The value longword, in D0.
 *
 * Original address: 0x00e15214
 * Re-emitted from the disassembly 0x00E15214-0x00E15220.
 */

#include "ec/ec_internal.h"

int32_t EC_$READ(ec_$eventcount_t *ec)
{
    /* 0x00E15218-0x00E1521C: movea.l (0x8,A6),A0 / move.l (A0),D0 */
    return ec->value;
}
