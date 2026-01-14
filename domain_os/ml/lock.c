/*
 * ML_$LOCK - Acquire a resource lock
 *
 * This function acquires a resource lock, blocking the calling process
 * if the lock is already held. Lock ordering is enforced to prevent
 * deadlocks - a process cannot acquire a lock with a lower ID than
 * any lock it currently holds.
 *
 * Original address: 0x00E20B12
 *
 * The original assembly uses a helper function at 0x00E20AE8 to handle
 * lock ordering validation and PCB updates before the actual lock attempt.
 */

#include "ml/ml_internal.h"

/* Internal helper: prepare for lock acquisition
 * This was a nested Pascal procedure in the original.
 * Validates lock ordering and updates PCB state.
 */
static void ml_$prepare_lock(int16_t resource_id)
{
    proc1_t *pcb = PROC1_$CURRENT_PCB;
    uint32_t lock_mask;

    /* Increment inhibit count (offset 0x5A in PCB) */
    pcb->pad_5a++;

    /* Calculate lock mask for this resource */
    lock_mask = 1U << (resource_id & 0x1F);

    /*
     * Lock ordering check: the new lock mask must be greater than
     * all currently held locks. This ensures locks are acquired
     * in ascending order, preventing deadlocks.
     */
    if (lock_mask <= pcb->resource_locks_held) {
        CRASH_SYSTEM(&Lock_ordering_violation);
    }

    /* Mark this lock as held in the PCB */
    pcb->resource_locks_held |= lock_mask;

    /* Reorder in ready list if we're not the head */
    if (pcb != PROC1_$READY_PCB) {
        proc1_$reorder_if_needed(pcb);
    }
}

void ML_$LOCK(int16_t resource_id)
{
    proc1_t *pcb;
    ec_$eventcount_t *ec_list[1];
    int32_t wait_vals[1];
    uint8_t old_lock_byte;
    int16_t ec_offset;

    /* Set up lock ordering and PCB state */
    ml_$prepare_lock(resource_id);
    pcb = PROC1_$CURRENT_PCB;

    for (;;) {
        /*
         * Try to acquire the lock using test-and-set on the lock byte.
         * In the original m68k code, this is done with BSET which is atomic.
         * The lock byte is at LOCK_BYTES[resource_id], bit 0 indicates held.
         */
        uint16_t sr;
        DISABLE_INTERRUPTS(sr);

        old_lock_byte = ML_$LOCK_BYTES[resource_id];
        ML_$LOCK_BYTES[resource_id] = old_lock_byte | 0x01;

        if ((old_lock_byte & 0x01) == 0) {
            /* Lock was free, we got it */
            ENABLE_INTERRUPTS(sr);
            return;
        }

        /*
         * Lock was already held - wait on the lock's event count.
         * Each lock has a 16-byte entry in the event list:
         *   +0x00: Event count structure
         *   +0x0C: Wait counter
         */
        ec_offset = resource_id << 4;

        /* Increment the wait count and use it as our wait value */
        ((int32_t *)((char *)ML_$LOCK_EVENTS + ec_offset + 0x0C))[0]++;
        wait_vals[0] = ((int32_t *)((char *)ML_$LOCK_EVENTS + ec_offset + 0x0C))[0];
        ec_list[0] = (ec_$eventcount_t *)((char *)ML_$LOCK_EVENTS + ec_offset);

        PROC1_$EC_WAITN(pcb, ec_list, wait_vals, 1);

        /* Loop back and try to acquire again */
    }
}
