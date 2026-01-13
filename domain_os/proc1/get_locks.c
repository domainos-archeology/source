/*
 * PROC1_$GET_LOCKS - Get locks held by current process
 *
 * Returns the resource_locks_held bitmask for the current process.
 *
 * Original address: 0x00e148e6
 */

#include "proc1.h"

uint32_t PROC1_$GET_LOCKS(void)
{
    return PROC1_$CURRENT_PCB->resource_locks_held;
}
