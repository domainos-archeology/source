/*
 * RIP_$LOCK / RIP_$UNLOCK - RIP subsystem locking
 *
 * Mutual exclusion for the routing table: the process lock 0x0E is taken
 * with PROC1_$SET_LOCK and then the RIP exclusion record is entered.
 *
 * Neither routine sets A5.  They address the exclusion record as (0x40,A5)
 * and rely on the caller's module base: every caller in the RIP_WIRED
 * object has done `lea (0xe26258).l,A5` (RIP_$AGE 0x00E155C8, RIP_$UPDATE_INT
 * 0x00E1592A, RIP_$PORT_CLOSE), so 0xE26258 + 0x40 is RIP_$DATA.exclusion.
 *
 * Original addresses:
 *   RIP_$LOCK:   0x00E154A4 (32 bytes; first symbol of `I E154A4 RIP_WIRED`)
 *   RIP_$UNLOCK: 0x00E154C4 (32 bytes)
 * Re-emitted from the disassembly 0x00E154A4-0x00E154E2.
 */

#include "rip/rip_internal.h"

/*
 * RIP_$LOCK - Acquire the RIP subsystem lock
 */
void RIP_$LOCK(void)
{
    /* 0x00E154A8-0x00E154B4: PROC1_$SET_LOCK(0xE) (with the compiler's
     * spare result slot). */
    PROC1_$SET_LOCK(RIP_LOCK_PRIORITY);

    /* 0x00E154B6-0x00E154BA: ML_$EXCLUSION_START(&(0x40,A5)). */
    ML_$EXCLUSION_START(&RIP_$WIRED_DATA.exclusion);
}

/*
 * RIP_$UNLOCK - Release the RIP subsystem lock
 */
void RIP_$UNLOCK(void)
{
    /* 0x00E154C8-0x00E154D2: ML_$EXCLUSION_STOP(&(0x40,A5)). */
    ML_$EXCLUSION_STOP(&RIP_$WIRED_DATA.exclusion);

    /* 0x00E154D4-0x00E154DA: PROC1_$CLR_LOCK(0xE). */
    PROC1_$CLR_LOCK(RIP_LOCK_PRIORITY);
}
