/*
 * PROC2_$MY_PID - Get current process ID
 *
 * Returns the PROC1 PID of the current process.
 * This is a simple wrapper around PROC1_$CURRENT.
 *
 * Returns:
 *   Current process ID (from PROC1)
 *
 * Original address: 0x00e40ca0
 */

#include "proc2/proc2_internal.h"

uint16_t PROC2_$MY_PID(void)
{
    return PROC1_$CURRENT;
}
