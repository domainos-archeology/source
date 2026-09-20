/*
 * PROC2_$GET_DEBUGGER_PID - Get the PROC1 PID of the current process's debugger
 *
 * Returns 0 when the current process has no debugger.
 *
 * Original address: 0x00e41b72 (70 bytes)
 *   0x00E41B7E..0x00E41B90  idx = PROC2_$PID_TO_INDEX[PROC1_$CURRENT]
 *   0x00E41B94..0x00E41B9C  A1 = 0xEA551C + idx*0xE4; (-0xBE,A1) = +0x26 debugger_idx
 *   0x00E41BA6..0x00E41BAC  (-0x4A,A0,dbg*0xE4) = debugger entry +0x9A level1_pid
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 */

#include "proc2/proc2_internal.h"

uint16_t PROC2_$GET_DEBUGGER_PID(void)
{
    int16_t my_index;
    uint16_t debugger_idx;

    my_index = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
    debugger_idx = P2_INFO_ENTRY(my_index)->debugger_idx;
    if (debugger_idx == 0) {
        /* 0x00E41BA2 clr.w D0w */
        return 0;
    }
    return P2_INFO_ENTRY(debugger_idx)->level1_pid;
}
