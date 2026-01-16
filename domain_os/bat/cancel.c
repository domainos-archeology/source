/*
 * BAT_$CANCEL - Cancel reserved blocks
 *
 * Moves blocks from the reserved pool back to the free pool.
 *
 * Original address: 0x00E3BAF6
 */

#include "bat/bat_internal.h"

/*
 * BAT_$CANCEL
 *
 * Parameters:
 *   vol_idx - Volume index (0-6)
 *   count   - Number of blocks to cancel
 *   status  - Output status code
 *
 * Assembly analysis:
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Validates count doesn't exceed reserved_blocks
 *   - Moves blocks from reserved_blocks to free_blocks
 */
void BAT_$CANCEL(int16_t vol_idx, uint32_t count, status_$t *status)
{
    bat_$volume_t *vol;

    ML_$LOCK(ML_LOCK_BAT);

    *status = status_$ok;
    vol = &bat_$volumes[vol_idx];

    /* Check if we have enough reserved blocks to cancel */
    if (vol->reserved_blocks < count) {
        *status = bat_$error;
    } else {
        /* Move blocks from reserved back to free pool */
        vol->free_blocks += count;
        vol->reserved_blocks -= count;
    }

    ML_$UNLOCK(ML_LOCK_BAT);
}
