/*
 * PROC1_$GET_LOCKS - Get locks held by current process
 *
 * Returns the resource_locks_held bitmask of the current process.
 *
 * 0x00E148E6  link.w A6,-0x4
 * 0x00E148EA  movea.l (0x00e1eac8).l,A0      ; PROC1_$CURRENT_PCB
 * 0x00E148F0  move.l (0x40,A0),D0            ; resource_locks_held
 * 0x00E148F4  unlk A6 / rts
 *
 * No arguments, no A5 load; the four-byte frame slot is never written.
 *
 * Original address: 0x00e148e6 (18 bytes)
 */

#include "proc1/proc1_internal.h"

uint32_t PROC1_$GET_LOCKS(void)
{
    /* 0x00E148EA..0x00E148F0 */
    return PROC1_$CURRENT_PCB->resource_locks_held;
}
