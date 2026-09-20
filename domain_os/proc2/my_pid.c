/*
 * PROC2_$MY_PID - Get current process's PROC1 PID
 *
 * Original address: 0x00e40ca0 (26 bytes)
 *   0x00E40CAC  move.w PROC1_$CURRENT (0x00E20608),D0w
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 */

#include "proc2/proc2_internal.h"

uint16_t PROC2_$MY_PID(void)
{
    return PROC1_$CURRENT;
}
