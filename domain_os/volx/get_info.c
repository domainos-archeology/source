/*
 * VOLX_$GET_INFO - Get volume information
 *
 * Returns the root directory UID and free/total block counts for a volume.
 *
 * Original address: 0x00E6B5C6
 */

#include "volx/volx_internal.h"

/*
 * VOLX_$GET_INFO
 *
 * Parameters:
 *   vol_idx     - Pointer to volume index (1-6)
 *   dir_uid_ret - Output: receives root directory UID
 *   free_blocks - Output: receives free block count
 *   total_blocks - Output: receives total block count
 *   status      - Output: status code
 *
 * Algorithm:
 *   1. Call BAT_$N_FREE to validate volume is mounted and get block counts
 *   2. If successful, return the dir_uid from the VOLX table entry
 *   3. Translate bat_$not_mounted to volume_logical_vol_not_mounted
 */
void VOLX_$GET_INFO(int16_t *vol_idx, uid_t *dir_uid_ret,
                    uint32_t *free_blocks, uint32_t *total_blocks,
                    status_$t *status)
{
    int16_t vol_idx_val;
    int16_t vol_idx_copy;
    volx_entry_t *entry;

    vol_idx_val = *vol_idx;
    vol_idx_copy = vol_idx_val;

    /* Get free/total block counts (also validates volume is mounted) */
    BAT_$N_FREE(&vol_idx_copy, free_blocks, total_blocks, status);

    if (*status == status_$ok) {
        /* Return the directory UID from the VOLX table */
        entry = &VOLX_$TABLE_BASE[vol_idx_val];
        *dir_uid_ret = entry->dir_uid;
    }

    /* Translate BAT status code to VOLX status code */
    if (*status == bat_$not_mounted) {
        *status = status_$volume_logical_vol_not_mounted;
    }
}
