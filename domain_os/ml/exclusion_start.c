/*
 * ML_$EXCLUSION_START - Enter an exclusion region
 *
 * Attempts to enter an exclusion region. If the region is already
 * occupied, the caller blocks until it becomes available.
 *
 * The exclusion lock state (f5) works as follows:
 *   -1: Unlocked (no one in region)
 *   0+: Locked, value is number of waiters
 *
 * When entering, we increment f5. If the result is 0, we're the first
 * one in (was -1 before). If positive, someone else is in and we wait.
 *
 * Original address: 0x00E20DF8
 */

#include "ml/ml_internal.h"
#include "proc1/proc1.h"

void ML_$EXCLUSION_START(ml_$exclusion_t *excl)
{
    proc1_t *pcb;
    ec_$eventcount_t *ec_list[1];
    int32_t wait_vals[1];

    pcb = PROC1_$CURRENT_PCB;

    /* Increment inhibit count - prevent preemption while in exclusion */
    pcb->nesting_depth++;

    /*
     * 0x00E20E04: bset.b #0x0,(0x43,A1).  0x43 is the least significant
     * byte of the longword at 0x40 (big-endian), so this sets bit 0 of
     * resource_locks_held -- the "holding an exclusion" marker that
     * ML_$EXCLUSION_STOP clears at 0x00E20EB0.
     */
    pcb->resource_locks_held |= 1u;

    /* Try to enter the exclusion region */
    excl->f5++;

    if (excl->f5 != 0) {
        /*
         * Someone else is in the region - we need to wait.
         * Increment the event count and wait for it.
         */
        /*
         * 0x00E20E10 "ori #0x700,SR".  The image RAISES the interrupt mask
         * and NEVER lowers it again: there is no SR restore anywhere between
         * here and the "rts" at 0x00E20E32, on either path.  PROC1_$EC_WAITN
         * is what eventually leaves the caller running with interrupts
         * enabled again.  Do not add a restore here - it would change the
         * interrupt state the caller is handed.  (source-l8qy)
         */
        uint16_t sr;
        DISABLE_INTERRUPTS(sr);
        (void)sr;               /* saved, and deliberately never restored */

        excl->f4++;
        wait_vals[0] = excl->f4;
        ec_list[0] = (ec_$eventcount_t *)excl;

        PROC1_$EC_WAITN(pcb, ec_list, wait_vals, 1);

        /*
         * PROC1_$EC_WAITN returns with interrupts enabled; the routine falls
         * straight into its "rts" (0x00E20E2C-0x00E20E32 restore only A3/A4).
         */
    }
}
