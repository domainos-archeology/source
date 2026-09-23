/*
 * EC_$INIT - Initialize a level-1 eventcount
 *
 * Zeroes the value and makes the waiter ring empty: both list pointers
 * point back at the eventcount itself, which is the ring's sentinel
 * (ec/sau2/advance_int.s walks it until it comes back to the eventcount).
 *
 * Parameters:
 *   ec - Pointer to the eventcount (argument 1, (0x8,A6))
 *
 * Original address: 0x00e151fe
 * Re-emitted from the disassembly 0x00E151FE-0x00E15212.
 */

#include "ec/ec_internal.h"

void EC_$INIT(ec_$eventcount_t *ec)
{
    /* 0x00E15206: clr.l (A0) */
    ec->value = 0;
    /* 0x00E15208-0x00E1520C: move.l A0,(0x4,A0) / move.l A0,(0x8,A0) */
    ec->waiter_list_head = (ec_$eventcount_waiter_t *)ec;
    ec->waiter_list_tail = (ec_$eventcount_waiter_t *)ec;
}
