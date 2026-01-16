/*
 * BAT_$ADD_PART_VTOCE - Add VTOCE to partition chain
 *
 * Updates the partition's VTOCE chain with a new or updated block.
 *
 * Original address: 0x00E3AE2E
 */

#include "bat/bat_internal.h"

/*
 * BAT_$ADD_PART_VTOCE
 *
 * Parameters:
 *   vol_idx - Volume index (0-6)
 *   block   - VTOCE block number
 *   status  - Output status code
 *
 * Returns:
 *   Previous VTOCE block in partition chain (24-bit value)
 *
 * Assembly analysis:
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Calculates partition index from block number
 *   - Swaps current partition VTOCE block with new block
 *   - Returns the old VTOCE block value
 */
uint32_t BAT_$ADD_PART_VTOCE(int16_t vol_idx, uint32_t block, status_$t *status)
{
    bat_$volume_t *vol;
    bat_$partition_t *part;
    int16_t partition_idx;
    uint32_t old_vtoce;

    ML_$LOCK(ML_LOCK_BAT);

    *status = status_$ok;
    vol = &bat_$volumes[vol_idx];

    /* Calculate partition index from block number */
    if (block < vol->partition_start_offset) {
        partition_idx = 0;
    } else {
        partition_idx = (int16_t)M$DIS$LLL(block - vol->partition_start_offset,
                                            vol->partition_size);
    }

    part = &vol->partitions[partition_idx];

    /* Get current VTOCE block (24-bit value) */
    old_vtoce = BAT_GET_VTOCE_BLOCK(part);

    /* Clear current VTOCE block and set new one */
    BAT_SET_VTOCE_BLOCK(part, block);

    ML_$UNLOCK(ML_LOCK_BAT);

    return old_vtoce;
}
