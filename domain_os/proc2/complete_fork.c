/*
 * PROC2_$COMPLETE_FORK - Complete fork in child process
 *
 * Advances the current process's fork-completion eventcount so the parent
 * blocked in PROC2_$FORK can proceed.  TRAP #0 syscall 0x19: takes no
 * arguments and returns nothing.
 *
 * Original address: 0x00e735f8 (64 bytes)
 *   0x00E735FE  idx = PROC2_$PID_TO_INDEX[PROC1_$CURRENT]
 *               (0xEA551C + pid*2 + 0x3EB6)
 *   0x00E73614  pea (-0x18,A1,D0) with A1 = 0xE2B978, D0 = idx*0x18
 *               -> PROC2_$EC[idx-1].fork_ec
 *   0x00E7362A  EC_$ADVANCE
 */

#include "proc2/proc2_internal.h"

void PROC2_$COMPLETE_FORK(void)
{
    int16_t current_idx;    /* D2 */

    current_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
    EC_$ADVANCE(PROC_FORK_EC(current_idx));
}
