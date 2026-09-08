/*
 * DIR_$UNLOCK_OBJ - Unlock/release lock on directory handle
 *
 * Releases the read or write lock on a directory. If the lock mode
 * is 2 (write) and the "dirty" flag (offset 0x20) is set, calls
 * AST_$PURIFY to flush changes before releasing.
 *
 * Decrements reader count (mode 1) or clears writer flag (mode 2).
 * If the waiter queue is empty and count reaches 0, returns the
 * lock entry to the free list. Otherwise, if there are waiters
 * and count reaches 0, advances the first waiter's event counter
 * to wake it up.
 *
 * Parameters:
 *   handle - Pointer to handle structure
 *
 * Handle fields used:
 *   +0x0A: Lock mode (2 bytes)
 *   +0x20: Dirty flag (1 byte, bit 7)
 *   +0x34: Lock entry pointer (4 bytes)
 *   +0x38: Event counter index (2 bytes)
 *
 * Lock entry fields:
 *   +0x00: Free list next (4 bytes)
 *   +0x08: Waiter queue head (4 bytes)
 *   +0x0C: Lock count (2 bytes)
 *   +0x0E: Slot index (2 bytes)
 *
 * Original address: 0x00E4B234
 * Original size: 264 bytes
 */

#include "dir/dir_internal.h"

void DIR_$UNLOCK_OBJ(void *handle)
{
    uint8_t *h = (uint8_t *)handle;
    int16_t mode;
    uint32_t *lock_entry;
    status_$t local_status;

    mode = *(int16_t *)(h + 0x0A);

    /* No-op if not locked */
    if (mode == 0) {
        return;
    }

    /* If write mode and dirty flag set, purify (flush) */
    if (mode == 2 && (int8_t)h[0x20] < 0) {
        AST_$PURIFY(handle, 0, 0, &DIR_$CONST_ZERO_L, 0, &local_status);
        if (local_status != status_$ok) {
            CRASH_SYSTEM(&local_status);
        }
    }

    ML_$EXCLUSION_START(&DIR_$MUTEX);

    lock_entry = *(uint32_t **)(h + 0x34);

    if (*(int16_t *)(h + 0x0A) == 1) {
        /* Reader mode: decrement count */
        if (*(int16_t *)((char *)lock_entry + 0x0C) < 1) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }
        *(int16_t *)((char *)lock_entry + 0x0C) -= 1;
    } else if (*(int16_t *)(h + 0x0A) == 2) {
        /* Writer mode: clear (must be -1) */
        if (*(int16_t *)((char *)lock_entry + 0x0C) != -1) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }
        *(int16_t *)((char *)lock_entry + 0x0C) = 0;
    }

    /* Check waiter queue */
    if (lock_entry[2] != 0) {
        /* Waiters present - if lock count is now 0, wake first waiter */
        if (*(int16_t *)((char *)lock_entry + 0x0C) == 0) {
            uint32_t first_waiter = lock_entry[2];
            int16_t ec_idx = *(int16_t *)(first_waiter + 0x38);
            EC_$ADVANCE((ec_$eventcount_t *)
                        ((char *)&DIR_$WAIT_ECS + (int16_t)(ec_idx * 0xC)));
        }
    } else {
        /* No waiters - if count is 0, free the lock entry */
        if (*(int16_t *)((char *)lock_entry + 0x0C) == 0) {
            char *base = (char *)__A5_BASE();
            lock_entry[0] = *(uint32_t *)(base + 0x2030);
            *(uint32_t *)(base + 0x2030) = *(uint32_t *)(h + 0x34);

            /* Clear bit in bitmap */
            uint32_t bit = 1u << (*(uint16_t *)((char *)lock_entry + 0x0E) & 0x1F);
            *(uint32_t *)(base + 0x2034) &= ~bit;
        }
    }

    /* Clear lock entry pointer */
    *(uint32_t *)(h + 0x34) = 0;

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);
}
