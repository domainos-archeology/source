/*
 * DIR_$LOCK_OBJ - Lock/open directory object
 *
 * Opens the directory object for the requested access mode.  Looks up a
 * dir_$lock_entry_t for the handle's UID in the 32-slot table at A5+0x1680
 * and, if there is none, takes one off the free list at A5+0x2030.  Then it
 * tries to acquire the lock, waiting on the slot's eventcount with a
 * timeout expressed against TIME_$CLOCKH.
 *
 * Lock modes:
 *   1 = reader (several allowed; lock_count counts them)
 *   2 = writer (exclusive; lock_count is -1)
 *
 * On failure (timeout) the handle's lock_mode is cleared and the lock entry
 * goes back on the free list if nobody else holds or wants it.
 *
 * Original address: 0x00E4AFA8
 * Original size: 644 bytes
 */

#include "dir/dir_internal.h"
#include "time/time.h"

/* `cmpi.w #0x9,(-0x2,A0,D0w*0x1)` at 0x00E4AFCA against PROC1_$TYPE. */
#define DIR_PROC_TYPE_NS_HELPER     9

/* `move.l #0x1e0,D4` (0x00E4B136) and `moveq #0x8,D4` (0x00E4B132). */
#define DIR_LOCK_WAIT_TICKS         0x1E0
#define DIR_LOCK_WAIT_TICKS_SHORT   8

void DIR_$LOCK_OBJ(void *handle, int16_t mode, status_$t *status_ret)
{
    char              *blk = DIR_$BLOCK;    /* the caller's A5 */
    dir_$handle_t     *h = (dir_$handle_t *)handle;   /* A3 */
    dir_$lock_entry_t *lock_entry;                    /* A2 */
    boolean            is_server;           /* D3 */
    int16_t            retry_limit;         /* D5, the `dbf` counter */
    int16_t            retry_count;         /* D6 */
    int16_t            i;                   /* D1, the slot scan index */
    int16_t            count;               /* D0, the `dbf` counter */
    int32_t            timeout;             /* D4 */

    /*
     * 0x00E4AFBC-0x00E4AFD0: PROC1_$TYPE[PROC1_$CURRENT] == 9 (the naming
     * server helper), turned into a Domain boolean by `seq D3b`.
     */
    is_server = (PROC1_$TYPE[PROC1_$CURRENT] == DIR_PROC_TYPE_NS_HELPER)
                ? true : false;

    h->lock_entry = 0;      /* 0x00E4AFD2 `clr.l (0x34,A3)` */
    h->lock_mode  = mode;   /* 0x00E4AFD6 `move.w D2w,(0xa,A3)` */

    ML_$EXCLUSION_START(&DIR_$MUTEX);       /* 0x00E4AFDA */

    /*
     * 0x00E4AFE8-0x00E4B020: scan all 32 slots.  The cursor starts at A5 and
     * advances 0x10 a turn, so slot i is at A5 + 0x1680 + i*0x10, and the
     * `cmp.w D1w,D4w` / `bcs` pair is the Pascal 0..31 range check.
     */
    i = 0;
    count = DIR_SLOT_COUNT - 1;
    do {
        if (i <= (int16_t)(DIR_SLOT_COUNT - 1) &&
            (DIR_LOCK_IN_USE_OF(blk) & (1u << ((uint32_t)i & 0x1F))) != 0) {
            dir_$lock_entry_t *slot = &DIR_LOCK_TAB_OF(blk)[i];

            /* 0x00E4B008-0x00E4B00E: `cmpm.l` over the two UID longwords. */
            if (slot->u.uid.high == h->uid.high &&
                slot->u.uid.low  == h->uid.low) {
                h->lock_entry = ARCH_PTR_TO_VA(slot);   /* 0x00E4B014 */
                goto found;
            }
        }
        i++;
        count--;
    } while (count != -1);

    /* 0x00E4B024-0x00E4B028: the scan may already have stored a pointer. */
    if (h->lock_entry != 0) {
        goto found;
    }

    /* 0x00E4B02A-0x00E4B05E: take the head of the free list. */
    h->lock_entry = DIR_LOCK_FREE_OF(blk);
    if (h->lock_entry == 0) {
        CRASH_SYSTEM(&Naming_bad_request_header_ver_err);   /* 0x00E4B032 */
    }

    lock_entry = (dir_$lock_entry_t *)ARCH_VA_TO_PTR(h->lock_entry);

    /* 0x00E4B042-0x00E4B04A: mark the slot busy in the bitmap. */
    DIR_LOCK_IN_USE_OF(blk) |= 1u << ((uint32_t)lock_entry->index & 0x1F);

    /* 0x00E4B04E: unlink it - `next` and `uid` alias, so read it first. */
    DIR_LOCK_FREE_OF(blk) = lock_entry->u.next;

    /* 0x00E4B052-0x00E4B058: the UID overwrites the free-list link. */
    lock_entry->u.uid.high = h->uid.high;
    lock_entry->u.uid.low  = h->uid.low;

    lock_entry->waiters    = 0;     /* 0x00E4B05A `clr.l (0x8,A0)` */
    lock_entry->lock_count = 0;     /* 0x00E4B05E `clr.w (0xc,A0)` */

found:
    /* 0x00E4B064: the pessimistic status the loop replaces on success. */
    *status_ret = status_$naming_directory_locked;      /* 0x000E0016 */
    lock_entry = (dir_$lock_entry_t *)ARCH_VA_TO_PTR(h->lock_entry);
    retry_limit = 2;        /* 0x00E4B06E `moveq #0x2,D5` */
    retry_count = 1;        /* 0x00E4B070 `moveq #0x1,D6` */

    do {
        int8_t can_acquire = 0;

        /*
         * 0x00E4B072-0x00E4B080: eligible when there is nobody queued OR
         * this is not the first attempt (`seq`/`sgt`, or'ed, tested `bpl`).
         */
        if (retry_count > 1 || lock_entry->waiters == 0) {
            /*
             * 0x00E4B082-0x00E4B09C: a writer needs lock_count == 0, a
             * reader needs lock_count >= 0 (`bmi` on the word).
             */
            if (mode == 2 && lock_entry->lock_count == 0) {
                can_acquire = 1;
            } else if (mode == 1 && lock_entry->lock_count >= 0) {
                can_acquire = 1;
            }
        }

        if (can_acquire) {
            if (mode == 1) {
                /* 0x00E4B0A4: `add.w D0w,(0xc,A2)` with D0 == 1. */
                lock_entry->lock_count += 1;

                /*
                 * 0x00E4B0A8-0x00E4B0D4: if the first queued handle also
                 * wants mode 1, wake its slot eventcount so the readers go
                 * in together.
                 */
                if (lock_entry->waiters != 0) {
                    dir_$handle_t *first = (dir_$handle_t *)
                        ARCH_VA_TO_PTR(lock_entry->waiters);
                    if (first->lock_mode == 1) {
                        EC_$ADVANCE(&DIR_$WAIT_ECS[first->slot_index]);
                    }
                }
            } else {
                /* 0x00E4B0D8 `move.w #-0x1,(0xc,A2)` */
                lock_entry->lock_count = -1;
            }

            *status_ret = status_$ok;   /* 0x00E4B0E0 `clr.l (A0)` */
            break;
        }

        /* 0x00E4B0E6-0x00E4B106: append this handle to the wait queue. */
        {
            uint32_t queue_ptr = lock_entry->waiters;
            if (queue_ptr == 0) {
                lock_entry->waiters = ARCH_PTR_TO_VA(h);
            } else {
                dir_$handle_t *q = (dir_$handle_t *)ARCH_VA_TO_PTR(queue_ptr);
                while (q->next != 0) {
                    q = (dir_$handle_t *)ARCH_VA_TO_PTR(q->next);
                }
                q->next = ARCH_PTR_TO_VA(h);
            }
            h->next = 0;        /* 0x00E4B106 `clr.l (0x30,A3)` */
        }

        /* 0x00E4B10A-0x00E4B114: the two wait counters. */
        if (retry_count > 2) {
            DIR_LK_WAIT_2_OF(blk) += 1;
        }
        DIR_LK_WAITS_OF(blk) += 1;

        /*
         * 0x00E4B118-0x00E4B13C: a naming-server helper whose per-process
         * flags word has bit 1 set waits only 8 ticks.
         */
        if (is_server < 0 &&
            (DIR_PROC_FLAGS_OF(blk, PROC1_$CURRENT) &
             DIR_PROC_FLAG_SHORT_LOCK_WAIT) != 0) {
            timeout = DIR_LOCK_WAIT_TICKS_SHORT;
        } else {
            timeout = DIR_LOCK_WAIT_TICKS;
        }

        {
            ec_$eventcount_t *slot_ec = &DIR_$WAIT_ECS[h->slot_index];
            /* 0x00E4B14E: the slot eventcount's current value + 1, read
             * while the mutex is still held. */
            int32_t slot_wait_val = (int32_t)slot_ec->value + 1;
            int16_t wake_idx;                               /* D4 */

            ML_$EXCLUSION_STOP(&DIR_$MUTEX);        /* 0x00E4B158 */

            /*
             * 0x00E4B166-0x00E4B19E.  EC_$WAIT takes two 3-element arrays BY
             * VALUE (24 bytes, popped with `lea (0x18,SP),SP`):
             *   ecs  = { &TIME_$CLOCKH, &DIR_$WAIT_ECS[slot_index], NULL }
             *   vals = { TIME_$CLOCKH + timeout, slot_wait_val, 0 }
             * TIME_$CLOCKH doubles as an eventcount here: waiting for it to
             * reach now+timeout is how the timeout is expressed.  It is
             * re-read at 0x00E4B16E, after the mutex has been released.
             * The result is the 0-based index of the eventcount that fired:
             * 0 means the clock (timeout), 1 means the lock slot.
             */
            wake_idx = EC_$WAIT(
                (ec_$wait_ecs_t){{ (ec_$eventcount_t *)&TIME_$CLOCKH,
                                   slot_ec, NULL }},
                (ec_$wait_vals_t){{ (int32_t)(TIME_$CLOCKH + timeout),
                                    slot_wait_val, 0 }});

            ML_$EXCLUSION_START(&DIR_$MUTEX);       /* 0x00E4B1A4 */

            /* 0x00E4B1B2-0x00E4B1D2: unlink this handle from the queue. */
            if (ARCH_PTR_TO_VA(h) == lock_entry->waiters) {
                lock_entry->waiters = h->next;
            } else {
                dir_$handle_t *prev = (dir_$handle_t *)
                    ARCH_VA_TO_PTR(lock_entry->waiters);
                while (prev->next != ARCH_PTR_TO_VA(h)) {
                    prev = (dir_$handle_t *)ARCH_VA_TO_PTR(prev->next);
                }
                prev->next = h->next;
            }

            if (wake_idx == 0) {
                /* 0x00E4B1D8-0x00E4B1DC: index 0 is the TIME_$CLOCKH
                 * eventcount, i.e. the wait timed out. */
                DIR_LK_TIMEOUTS_OF(blk) += 1;
                break;
            }
        }

        retry_count++;
        retry_limit--;
    } while (retry_limit != -1);

    /* 0x00E4B1E8-0x00E4B216: on failure, undo everything. */
    if (*status_ret != status_$ok) {
        h->lock_mode = 0;       /* 0x00E4B1EE `clr.w (0xa,A3)` */

        /* Free the entry only when nobody holds it and nobody is queued. */
        if (lock_entry->lock_count == 0 && lock_entry->waiters == 0) {
            /* 0x00E4B1FE / 0x00E4B202: push it back on the free list.  The
             * link overwrites the first UID longword. */
            lock_entry->u.next = DIR_LOCK_FREE_OF(blk);
            DIR_LOCK_FREE_OF(blk) = h->lock_entry;

            /* 0x00E4B208-0x00E4B212 */
            DIR_LOCK_IN_USE_OF(blk) &=
                ~(1u << ((uint32_t)lock_entry->index & 0x1F));

            h->lock_entry = 0;  /* 0x00E4B216 */
        }
    }

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);        /* 0x00E4B21A */
}
