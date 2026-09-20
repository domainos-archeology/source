/*
 * PROC2_$DELIVER_PENDING - Deliver pending signals to current process
 *
 * TRAP #0 syscall 0x18: takes no arguments and returns nothing.  Under the
 * PROC2 lock it re-enables quit delivery for the current address space and
 * hands whatever is pending to PROC2_$DELIVER_PENDING_INTERNAL.
 *
 * Original address: 0x00e3f520 (98 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 */

#include "proc2/proc2_internal.h"

void PROC2_$DELIVER_PENDING(void)
{
    int16_t current_idx;    /* (-0x2,A6) */

    /* 0x00E3F52C..0x00E3F53E: idx = PROC2_$PID_TO_INDEX[PROC1_$CURRENT] */
    current_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* 0x00E3F544 ML_$LOCK(4) */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F552..0x00E3F55E: clr.b FIM_$QUIT_INH[PROC1_$AS_ID] */
    FIM_$QUIT_INH[PROC1_$AS_ID] = 0;

    /* 0x00E3F562 */
    PROC2_$DELIVER_PENDING_INTERNAL(current_idx);

    /* 0x00E3F56E ML_$UNLOCK(4) */
    ML_$UNLOCK(PROC2_LOCK_ID);
}
