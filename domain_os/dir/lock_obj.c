/*
 * DIR_$LOCK_OBJ - Lock/open directory object
 *
 * Opens the directory object for the specified access mode. Looks up
 * a lock entry for the UID in a global table (32 slots). If no existing
 * entry is found, allocates one from a free list. Then attempts to
 * acquire the requested lock (read or write) with retry and timeout.
 *
 * Lock modes:
 *   1 = Reader (multiple readers allowed)
 *   2 = Writer (exclusive)
 *
 * The lock table entries are 16 bytes each at offset 0x1680 from A5.
 * Each entry contains:
 *   +0x00: UID high (4 bytes)
 *   +0x04: UID low (4 bytes)
 *   +0x08: Waiter queue head (4 bytes)
 *   +0x0C: Lock count (2 bytes) - positive=readers, -1=writer
 *   +0x0E: Slot index (2 bytes)
 *
 * The handle structure fields used:
 *   +0x0A: Lock mode (2 bytes)
 *   +0x30: Wait queue next pointer (4 bytes)
 *   +0x34: Lock entry pointer (4 bytes)
 *   +0x38: Event counter index (2 bytes)
 *
 * On failure (timeout), clears handle's lock mode and releases the
 * lock entry back to the free list if no other waiters/holders.
 *
 * Original address: 0x00E4AFA8
 * Original size: 644 bytes
 */

#include "dir/dir_internal.h"
#include "time/time.h"

void DIR_$LOCK_OBJ(void *handle, int16_t mode, status_$t *status_ret)
{
    uint32_t *h = (uint32_t *)handle;
    boolean is_server;
    uint32_t *lock_entry;
    int16_t retry_limit;
    int16_t retry_count;
    uint32_t i;
    int32_t timeout;

    /* Check if current process is server type (type 9).
     * 0xE4AFBC: `cmpi.w #0x9,(-0x2,A0,D0w*1)` with A0 = 0xE2612C and
     * D0 = PROC1_$CURRENT*2, i.e. PROC1_$TYPE[PROC1_$CURRENT] against the
     * 0xE2612A base declared in proc1.h; `seq D3b` yields 0xFF / 0x00. */
    is_server = (PROC1_$TYPE[PROC1_$CURRENT] == 9) ? true : false;

    /* Clear lock entry pointer and set mode in handle */
    h[0x0D] = 0;                                /* handle+0x34: lock entry */
    *(int16_t *)((char *)h + 0x0A) = mode;      /* handle+0x0A: lock mode */

    ML_$EXCLUSION_START(&DIR_$MUTEX);

    /* Search for existing lock entry matching this UID */
    {
        char *base = (char *)__A5_BASE();
        char *scan = base;
        int16_t count = 0x1F;

        i = 0;
        do {
            uint32_t bitmap = *(uint32_t *)(base + 0x2034);
            if ((uint16_t)i < 0x20 && (bitmap & (1u << (i & 0x1F))) != 0) {
                uint32_t *slot = (uint32_t *)(scan + 0x1680);
                if (slot[0] == h[0] && slot[1] == h[1]) {
                    /* Found matching lock entry */
                    h[0x0D] = (uint32_t)(uintptr_t)(scan + 0x1680);
                    goto found;
                }
            }
            i = (uint32_t)((uint16_t)i + 1);
            scan += 0x10;
            count--;
        } while (count != -1);
    }

    /* Not found - check if pointer was set (it wasn't) */
    if (h[0x0D] != 0) {
        goto found;
    }

    /* Allocate from free list */
    {
        char *base = (char *)__A5_BASE();
        uint32_t free_head = *(uint32_t *)(base + 0x2030);

        h[0x0D] = free_head;
        if (free_head == 0) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }

        lock_entry = (uint32_t *)h[0x0D];

        /* Mark slot as active in bitmap */
        {
            uint32_t bit = 1u << (*(uint16_t *)((char *)lock_entry + 0x0E) & 0x1F);
            *(uint32_t *)(base + 0x2034) |= bit;
        }

        /* Remove from free list */
        *(uint32_t *)(base + 0x2030) = lock_entry[0];

        /* Copy UID into lock entry */
        lock_entry[0] = h[0];
        lock_entry[1] = h[1];

        /* Initialize: no waiters, no lock holders */
        lock_entry[2] = 0;         /* waiter queue head */
        *(int16_t *)(lock_entry + 3) = 0;   /* lock count */
    }

found:
    *status_ret = status_$naming_directory_locked;  /* 0x000E0016 */
    lock_entry = (uint32_t *)h[0x0D];
    retry_limit = 2;
    retry_count = 1;

    do {
        int8_t can_acquire = 0;

        /* Check if lock can be acquired:
         * 1. retry_count > 1 (first iteration is always eligible), OR
         * 2. No waiters (lock_entry[2] == 0) */
        if (retry_count > 1 || lock_entry[2] == 0) {
            /* Additionally check mode compatibility:
             * - mode 2 (writer): needs lock_count == 0
             * - mode 1 (reader): needs lock_count >= 0 (no writer) */
            if (mode == 2 && *(int16_t *)(lock_entry + 3) == 0) {
                can_acquire = 1;
            } else if (mode == 1 && *(int16_t *)(lock_entry + 3) >= 0) {
                can_acquire = 1;
            }
        }

        if (can_acquire) {
            if (mode == 1) {
                /* Reader: increment count */
                *(int16_t *)(lock_entry + 3) += 1;

                /* If there are waiters and the first waiter wants mode 1,
                 * wake it up */
                if (lock_entry[2] != 0) {
                    uint32_t waiter = lock_entry[2];
                    if (*(int16_t *)(waiter + 0x0A) == 1) {
                        int16_t ec_idx = *(int16_t *)(waiter + 0x38);
                        EC_$ADVANCE((ec_$eventcount_t *)
                                    ((char *)&DIR_$WAIT_ECS + (int16_t)(ec_idx * 0xC)));
                    }
                }
            } else {
                /* Writer: set count to -1 */
                *(int16_t *)(lock_entry + 3) = -1;
            }

            *status_ret = status_$ok;
            break;
        }

        /* Cannot acquire - add to wait queue */
        {
            uint32_t queue_ptr = lock_entry[2];
            if (queue_ptr == 0) {
                lock_entry[2] = (uint32_t)(uintptr_t)h;
            } else {
                while (*(uint32_t *)(queue_ptr + 0x30) != 0) {
                    queue_ptr = *(uint32_t *)(queue_ptr + 0x30);
                }
                *(uint32_t *)(queue_ptr + 0x30) = (uint32_t)(uintptr_t)h;
            }
            h[0x0C] = 0;  /* handle+0x30: next waiter = NULL */
        }

        /* Increment contention counters */
        {
            char *base = (char *)__A5_BASE();
            if (retry_count > 2) {
                *(uint32_t *)(base + 0x2028) += 1;
            }
            *(uint32_t *)(base + 0x202C) += 1;
        }

        /* Determine timeout: server processes with bit 1 set get short timeout (8),
         * otherwise normal timeout (0x1E0) */
        if (is_server < 0) {
            char *base = (char *)__A5_BASE();
            uint16_t proc_flags = *(uint16_t *)(base + (int16_t)(PROC1_$CURRENT * 2) + 0x15FE);
            if (proc_flags & 2) {
                timeout = 8;
            } else {
                timeout = 0x1E0;
            }
        } else {
            timeout = 0x1E0;
        }

        /* Wait for lock to become available */
        {
            int16_t ec_idx = *(int16_t *)((char *)h + 0x38);
            ec_$eventcount_t *slot_ec = (ec_$eventcount_t *)
                          ((char *)&DIR_$WAIT_ECS + (int16_t)(ec_idx * 0xC));
            /* 0xE4B14E: the slot eventcount's current value + 1, read while
             * the mutex is still held. */
            int32_t slot_wait_val = (int32_t)slot_ec->value + 1;
            int16_t wake_idx;

            ML_$EXCLUSION_STOP(&DIR_$MUTEX);        /* 0xE4B158 */

            /*
             * 0xE4B166-0xE4B19E.  EC_$WAIT takes two 3-element arrays BY
             * VALUE (24 bytes, popped with `lea (0x18,SP),SP`):
             *   ecs  = { &TIME_$CLOCKH, &DIR_$WAIT_ECS[ec_idx], NULL }
             *   vals = { TIME_$CLOCKH + timeout, slot_wait_val, 0 }
             * TIME_$CLOCKH doubles as an eventcount here: waiting for it to
             * reach now+timeout is how the timeout is expressed.  It is
             * re-read at 0xE4B16E, after the mutex has been released.
             * The result is the 0-based index of the eventcount that fired:
             * 0 means the clock (timeout), 1 means the lock slot.
             */
            wake_idx = EC_$WAIT(
                (ec_$wait_ecs_t){{ (ec_$eventcount_t *)&TIME_$CLOCKH,
                                   slot_ec, NULL }},
                (ec_$wait_vals_t){{ (int32_t)(TIME_$CLOCKH + timeout),
                                    slot_wait_val, 0 }});

            ML_$EXCLUSION_START(&DIR_$MUTEX);       /* 0xE4B1A4 */

            /* Remove ourselves from wait queue */
            if (h == (uint32_t *)lock_entry[2]) {
                lock_entry[2] = h[0x0C];  /* h->next */
            } else {
                uint32_t prev = lock_entry[2];
                while ((uint32_t *)*(uint32_t *)(prev + 0x30) != h) {
                    prev = *(uint32_t *)(prev + 0x30);
                }
                *(uint32_t *)(prev + 0x30) = h[0x0C];
            }

            if (wake_idx == 0) {
                /* 0xE4B1D8: index 0 == the TIME_$CLOCKH eventcount, i.e. the
                 * wait timed out. */
                char *base = (char *)__A5_BASE();
                *(uint32_t *)(base + 0x2024) += 1;
                break;
            }
        }

        retry_count++;
        retry_limit--;
    } while (retry_limit != -1);

    /* On failure: clean up */
    if (*status_ret != status_$ok) {
        *(int16_t *)((char *)h + 0x0A) = 0;  /* Clear lock mode */

        /* If lock has no holders and no waiters, free the entry */
        if (*(int16_t *)(lock_entry + 3) == 0 && lock_entry[2] == 0) {
            char *base = (char *)__A5_BASE();
            lock_entry[0] = *(uint32_t *)(base + 0x2030);
            *(uint32_t *)(base + 0x2030) = h[0x0D];

            /* Clear bit in bitmap */
            uint32_t bit = 1u << (*(uint16_t *)((char *)lock_entry + 0x0E) & 0x1F);
            *(uint32_t *)(base + 0x2034) &= ~bit;

            h[0x0D] = 0;
        }
    }

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);
}
