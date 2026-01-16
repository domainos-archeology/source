/*
 * BAT_$N_FREE - Get free block count for a volume
 *
 * Returns the number of free and total blocks on a volume.
 *
 * Original address: 0x00E3B9E4
 */

#include "bat/bat_internal.h"
#include "network/network.h"

/*
 * BAT_$N_FREE
 *
 * Parameters:
 *   vol_idx_ptr - Pointer to volume index
 *   free_out    - Output receiving free block count
 *   total_out   - Output receiving total block count
 *   status      - Output status code
 *
 * Assembly analysis:
 *   - Checks NETWORK_$REALLY_DISKLESS flag first
 *   - Takes ML_LOCK_BAT (0x11) for thread safety
 *   - Validates volume index (1-6) and mount status
 *   - Returns free_blocks and total_blocks from volume structure
 */
void BAT_$N_FREE(uint16_t *vol_idx_ptr, uint32_t *free_out, uint32_t *total_out,
                 status_$t *status)
{
    uint16_t vol_idx;
    status_$t local_status;

    vol_idx = *vol_idx_ptr;

    /* Check if system is diskless */
    if (NETWORK_$REALLY_DISKLESS < 0) {
        *status = bat_$not_mounted;
        return;
    }

    ML_$LOCK(ML_LOCK_BAT);

    /* Validate volume index and mount status */
    if (vol_idx == 0 || vol_idx > 6 || bat_$mounted[vol_idx] >= 0) {
        local_status = bat_$not_mounted;
    } else {
        /* Volume is mounted, return statistics */
        *free_out = bat_$volumes[vol_idx].free_blocks;
        *total_out = bat_$volumes[vol_idx].total_blocks;
        local_status = status_$ok;
    }

    ML_$UNLOCK(ML_LOCK_BAT);

    *status = local_status;
}
