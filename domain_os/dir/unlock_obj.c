/*
 * DIR_$UNLOCK_OBJ - Release the lock on a directory handle
 *
 * Releases the read or write lock the handle holds.  A write lock on a
 * handle whose `mapped` flag is set is purified (flushed) first.
 *
 * Reader release decrements dir_$lock_entry_t.lock_count, writer release
 * puts it back to 0; both crash if the count does not match the mode.  Once
 * the count reaches 0 the entry either wakes the first queued handle, or -
 * if nobody is queued - goes back on the free list at A5+0x2030 and its bit
 * is cleared in the in-use bitmap at A5+0x2034.
 *
 * Original address: 0x00E4B234
 * Original size: 264 bytes
 */

#include "dir/dir_internal.h"

void DIR_$UNLOCK_OBJ(void *handle)
{
    dir_$handle_t     *h = (dir_$handle_t *)handle;      /* A2 */
    dir_$lock_entry_t *lock_entry;                       /* A3 */
    int16_t            mode;                             /* D0 */
    status_$t          local_status;                     /* A6-0x08 */

    mode = h->lock_mode;    /* 0x00E4B240 */

    /* 0x00E4B244: an unlocked handle is a no-op. */
    if (mode == 0) {
        return;
    }

    /*
     * 0x00E4B248-0x00E4B27E: a write lock on a mapped handle is flushed
     * first.  The `clr.l -(SP)` at 0x00E4B260 covers BOTH the flags word and
     * the segment word, and `pea (0xde,PC)` at 0x00E4B25C resolves to
     * 0x00E4B33C - DIR_$CONST_ZERO_L, the "no segment list" longword.
     */
    if (mode == 2 && h->mapped < 0) {
        AST_$PURIFY(&h->uid, 0, 0, &DIR_$CONST_ZERO_L, 0, &local_status);
        if (local_status != status_$ok) {
            CRASH_SYSTEM(&local_status);        /* 0x00E4B274 */
        }
    }

    ML_$EXCLUSION_START(&DIR_$MUTEX);           /* 0x00E4B280 */

    lock_entry = (dir_$lock_entry_t *)ARCH_VA_TO_PTR(h->lock_entry);

    /* 0x00E4B292-0x00E4B2CE: the lock_mode word is re-read from the handle
     * here rather than reusing D0. */
    if (h->lock_mode == 1) {
        /* 0x00E4B29A `tst.w (0xc,A3)` / `bgt`: a reader release needs a
         * count of at least 1. */
        if (lock_entry->lock_count < 1) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }
        lock_entry->lock_count -= 1;            /* 0x00E4B2AC */
    } else if (h->lock_mode == 2) {
        /* 0x00E4B2BA: a writer release needs exactly -1. */
        if (lock_entry->lock_count != -1) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }
        lock_entry->lock_count = 0;             /* 0x00E4B2CE */
    }

    if (lock_entry->waiters != 0) {             /* 0x00E4B2D2 */
        /* 0x00E4B2DE-0x00E4B300: with the lock now free, wake the first
         * queued handle through its own slot eventcount. */
        if (lock_entry->lock_count == 0) {
            dir_$handle_t *first = (dir_$handle_t *)
                ARCH_VA_TO_PTR(lock_entry->waiters);
            EC_$ADVANCE(&DIR_$WAIT_ECS[first->slot_index]);
        }
    } else {
        /* 0x00E4B304-0x00E4B31E: nobody queued, so return the entry. */
        if (lock_entry->lock_count == 0) {
            /* The link overwrites the first UID longword; `next` and `uid`
             * alias by design. */
            lock_entry->u.next = DIR_$DATA.lock_free;
            DIR_$DATA.lock_free = h->lock_entry;

            DIR_$DATA.lock_in_use &=
                ~(1u << ((uint32_t)lock_entry->index & 0x1F));
        }
    }

    h->lock_entry = 0;                          /* 0x00E4B322 */

    ML_$EXCLUSION_STOP(&DIR_$MUTEX);            /* 0x00E4B326 */
}
