/*
 * DBUF_$UPDATE_VOL - Flush dirty buffers for a volume
 *
 * Writes all dirty buffers for a specific volume (or all volumes) to disk.
 * Used during volume sync operations and before dismount.
 *
 * Original address: 0x00e3aaa2
 */

#include "dbuf/dbuf_internal.h"

/*
 * DBUF_$UPDATE_VOL
 *
 * Parameters:
 *   vol_idx - Volume index to flush (0 = all volumes)
 *   uid_p   - Reserved (unused)
 *
 * Notes:
 *   - Iterates through all buffer entries
 *   - For each dirty buffer matching the volume, writes it to disk
 *   - Skips buffers that are busy or have non-zero reference counts
 *   - Does not block on busy buffers; simply skips them
 */
void DBUF_$UPDATE_VOL(uint16_t vol_idx, void *uid_p)
{
    int16_t count;
    dbuf_$entry_t *entry;
    uint16_t token;
    status_$t local_status;
    dbuf_$write_params_t write_params;
    int i;
    uint8_t *src;
    uint8_t *dst;

    (void)uid_p;  /* Unused parameter */

    count = dbuf_$count - 1;
    if (count < 0) {
        return;
    }

    entry = &DBUF;

    do {
        /* Check if buffer is dirty (valid bit set = 0x4000) */
        if (!DBUF_IS_VALID(entry)) {
            goto next_entry;
        }

        /* Check volume match (if vol_idx != 0) */
        if (vol_idx != 0 && DBUF_GET_VOL(entry) != vol_idx) {
            goto next_entry;
        }

        /* Acquire spin lock to check/set flags atomically */
        token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);

        /*
         * Recheck conditions under lock:
         * - Volume matches (or vol_idx is 0)
         * - Buffer is dirty (0x4000)
         * - No references (ref_count == 0)
         * - Not busy
         */
        if ((vol_idx == 0 || DBUF_GET_VOL(entry) == vol_idx) &&
            DBUF_IS_VALID(entry) &&
            entry->ref_count == 0 &&
            !(entry->flags & DBUF_ENTRY_BUSY)) {

            /* Mark buffer as busy */
            entry->flags |= DBUF_ENTRY_BUSY;
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);

            /* Copy write params from buffer */
            src = (uint8_t *)&entry->uid;
            dst = (uint8_t *)&write_params;
            for (i = 0; i < 12; i++) {
                *dst++ = *src++;
            }
            write_params.type = entry->type;
            write_params.reserved = 0;

            /* Clear dirty flag */
            entry->flags &= ~DBUF_ENTRY_DIRTY;

            /* Write buffer to disk */
            DISK_$WRITE(DBUF_GET_VOL(entry), (void *)(uintptr_t)entry->block,
                        (void *)(uintptr_t)entry->ppn, &write_params, &local_status);

            if (local_status != status_$ok) {
                /* Mark volume as having trouble */
                DBUF_$TROUBLE |= (1 << DBUF_GET_VOL(entry));
            }

            /* Reacquire lock to clear busy flag */
            token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
            entry->flags &= ~DBUF_ENTRY_BUSY;

            /* Wake any waiters */
            if (dbuf_$waiters != 0) {
                EC_$ADVANCE(&dbuf_$eventcount);
            }
        }

        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);

next_entry:
        /* Move to next entry */
        entry = (dbuf_$entry_t *)((char *)entry + DBUF_ENTRY_SIZE);
        count--;
    } while (count >= 0);
}
