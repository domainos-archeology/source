/*
 * DBUF_$INVALIDATE - Invalidate disk buffers
 *
 * Invalidates cached buffers for a specific block or all blocks on a volume.
 * Used when disk contents have changed externally or volume is being dismounted.
 *
 * Original address: 0x00e3a9ec
 */

#include "dbuf/dbuf_internal.h"

/*
 * DBUF_$INVALIDATE
 *
 * Parameters:
 *   block   - Block number to invalidate (0 = all blocks on volume)
 *   vol_idx - Volume index (0-7)
 *
 * Notes:
 *   - If block is 0, invalidates ALL buffers for the volume
 *   - If block is non-zero, invalidates only that specific block
 *   - Clears the DBUF_$TROUBLE flag for the volume
 *   - Does NOT wait for busy buffers - marks them invalid anyway
 */
void DBUF_$INVALIDATE(int32_t block, uint16_t vol_idx)
{
    uint16_t token;
    int16_t count;
    dbuf_$entry_t *entry;

    count = dbuf_$count - 1;
    if (count < 0) {
        goto clear_trouble;
    }

    entry = &DBUF;

    do {
        /* Check if this buffer belongs to the target volume */
        if (DBUF_GET_VOL(entry) == vol_idx) {
            /* If block is 0, invalidate all; otherwise check specific block */
            if (block == 0 || entry->block == block) {
                /* Clear volume bits */
                entry->flags &= ~DBUF_ENTRY_VOL_MASK;

                /* Mark block as invalid */
                entry->block = -1;

                /* Acquire spin lock to clear busy flag safely */
                token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
                entry->flags &= ~DBUF_ENTRY_BUSY;

                /* Wake waiters if any */
                if (dbuf_$waiters != 0) {
                    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
                    EC_$ADVANCE(&dbuf_$eventcount);
                } else {
                    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
                }

                /* Clear dirty flag */
                entry->flags &= ~DBUF_ENTRY_DIRTY;

                /* Clear reference count */
                entry->ref_count = 0;

                /* If specific block requested and found, we're done */
                if (block != 0) {
                    break;
                }
            }
        }

        /* Move to next entry */
        entry = (dbuf_$entry_t *)((char *)entry + DBUF_ENTRY_SIZE);
        count--;
    } while (count >= 0);

clear_trouble:
    /* Clear trouble bit for this volume */
    DBUF_$TROUBLE &= ~(1 << vol_idx);
}
