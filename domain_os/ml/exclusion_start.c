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

#include "ml.h"
#include "proc1/proc1.h"

void ML_$EXCLUSION_START(ml_$exclusion_t *excl)
{
    proc1_t *pcb;
    ec_$eventcount_t *ec_list[1];
    int32_t wait_vals[1];

    pcb = PROC1_$CURRENT_PCB;

    /* Increment inhibit count - prevent preemption while in exclusion */
    pcb->pad_5a++;

    /* Set "has locks" flag in PCB (bit 0 at offset 0x43) */
    *((uint8_t *)&pcb->resource_locks_held + 3) |= 0x01;

    /* Try to enter the exclusion region */
    excl->f5++;

    if (excl->f5 != 0) {
        /*
         * Someone else is in the region - we need to wait.
         * Increment the event count and wait for it.
         */
        DISABLE_INTERRUPTS();

        excl->f4++;
        wait_vals[0] = excl->f4;
        ec_list[0] = (ec_$eventcount_t *)excl;

        PROC1_$EC_WAITN(pcb, ec_list, wait_vals, 1);

        /* PROC1_$EC_WAITN returns with interrupts enabled */
    }
}
