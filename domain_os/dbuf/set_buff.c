/*
 * DBUF_$SET_BUFF - Release/update a disk buffer
 *
 * Releases a buffer obtained from DBUF_$GET_BLOCK and optionally marks
 * it for writeback.
 *
 * Original address: 0x00e3a8b6
 */

#include "dbuf/dbuf_internal.h"

/*
 * DBUF_$SET_BUFF
 *
 * Parameters:
 *   buffer - Pointer to the buffer data (from DBUF_$GET_BLOCK)
 *   flags  - Buffer operation flags:
 *            0x01 = mark dirty
 *            0x02 = write back if dirty
 *            0x04 = invalidate buffer
 *            0x08 = decrement reference count (release)
 *   status - Receives status code
 */
void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    uint16_t token;
    dbuf_$entry_t *entry;
    dbuf_$write_params_t write_params;
    int i;
    uint8_t *src;
    uint8_t *dst;

    /* Acquire spin lock to search buffer list */
    token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);

    /* Find the buffer entry that owns this data pointer */
    entry = dbuf_$head;
    while (entry != NULL) {
        if (entry->data == buffer) {
            break;
        }
        entry = entry->next;
    }

    /* If buffer not found, crash - caller passed bad pointer */
    if (entry == NULL) {
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
        CRASH_SYSTEM(&OS_DBUF_bad_ptr_err);
        return;  /* Not reached */
    }

    /* Release spin lock - we found our entry */
    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);

    *status = status_$ok;

    /*
     * Flag 0x01: Mark buffer as dirty
     */
    if (flags & DBUF_FLAG_DIRTY) {
        entry->flags |= DBUF_ENTRY_DIRTY;
    }

    /*
     * Flag 0x02: Write back if dirty
     * Only writes if buffer is marked dirty (bit 14 in word = 0x4000)
     */
    if ((flags & DBUF_FLAG_WRITEBACK) && DBUF_IS_VALID(entry)) {
        /* Copy UID and hint to write params */
        src = (uint8_t *)&entry->uid;
        dst = (uint8_t *)&write_params;
        for (i = 0; i < 12; i++) {
            *dst++ = *src++;
        }
        write_params.type = entry->type;
        write_params.reserved = 0;

        /* Clear dirty flag before write */
        entry->flags &= ~DBUF_ENTRY_DIRTY;

        /* Write buffer to disk */
        DISK_$WRITE(DBUF_GET_VOL(entry), (void *)(uintptr_t)entry->block,
                    (void *)(uintptr_t)entry->ppn, &write_params, status);

        if (*status != status_$ok) {
            /* Mark volume as having trouble */
            DBUF_$TROUBLE |= (1 << DBUF_GET_VOL(entry));
        }
    }

    /*
     * Flag 0x04: Invalidate buffer
     * Clears volume and block, marks buffer as not having valid data
     */
    if (flags & DBUF_FLAG_INVALIDATE) {
        entry->flags &= ~DBUF_ENTRY_VOL_MASK;
        entry->block = -1;
        entry->flags &= ~DBUF_ENTRY_DIRTY;
    }

    /*
     * Flag 0x08: Decrement reference count (release)
     */
    if (flags & DBUF_FLAG_RELEASE) {
        if (entry->ref_count == 0) {
            /* Releasing with ref_count already 0 is a bug */
            CRASH_SYSTEM(&OS_DBUF_bad_free_err);
            return;  /* Not reached */
        }

        entry->ref_count--;

        /* If ref count dropped to 0 and there are waiters, wake them */
        if (entry->ref_count == 0 && dbuf_$waiters != 0) {
            EC_$ADVANCE(&dbuf_$eventcount);
        }
    }
}
