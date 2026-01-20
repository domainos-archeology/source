/*
 * VOLX_$REC_ENTRY - Record entry directory UID
 *
 * Updates the root directory UID for a mounted volume.
 * Used to update the entry after initial mount or when the
 * root directory changes.
 *
 * Original address: 0x00E6B6B2
 */

#include "volx/volx_internal.h"

/*
 * VOLX_$REC_ENTRY
 *
 * Parameters:
 *   vol_idx     - Pointer to volume index (1-6)
 *   dir_uid     - Pointer to new root directory UID
 *
 * Notes:
 *   - This function does not validate that vol_idx is in range
 *   - No status return - caller must ensure valid volume index
 */
void VOLX_$REC_ENTRY(int16_t *vol_idx, uid_t *dir_uid)
{
    int16_t vol_idx_val;
    volx_entry_t *entry;

    vol_idx_val = *vol_idx;
    entry = &VOLX_$TABLE_BASE[vol_idx_val];

    /* Update the directory UID */
    entry->dir_uid = *dir_uid;
}
