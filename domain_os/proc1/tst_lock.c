/*
 * PROC1_$TST_LOCK - Does the current process hold a resource lock?
 * Original address: 0x00e148ca (28 bytes)
 *
 * 0x00E148CA  link.w A6,-0x4
 * 0x00E148CE  A0 = PROC1_$CURRENT_PCB (0xE1EAC8)
 * 0x00E148D4  D0 = lock_id (0x8,A6)
 * 0x00E148D8  D1 = (0x40,A0)                         resource_locks_held
 * 0x00E148DC  btst.l D0,D1                           bit (lock_id mod 32)
 * 0x00E148DE  sne D1b / move.b D1b,D0b               boolean into D0.b; the
 *                                                    upper bytes of D0 still
 *                                                    hold lock_id
 * 0x00E148E2  unlk / rts
 *
 * Callers (PROC2_$CREATE 0x00E4155E, PROC2_$FORK) test the byte with
 * `tst.b / bmi'.
 *
 * Parameters:
 *   lock_id - the lock number; `btst.l Dn' uses it modulo 32
 *
 * Returns:
 *   Domain boolean: 0xFF when the bit is set
 */

#include "proc1/proc1_internal.h"

int8_t PROC1_$TST_LOCK(uint16_t lock_id)
{
    /* 0x00E148DC / 0x00E148DE */
    return ((PROC1_$CURRENT_PCB->resource_locks_held >> (lock_id & 0x1F)) & 1u) ? -1 : 0;
}
