/*
 * BAT_$RESERVE - Reserve blocks for future allocation
 *
 * Moves blocks from the free pool to the reserved pool.
 * Reserved blocks can only be allocated with the reserved flag.
 *
 * Original address: 0x00E3BA74
 */

#include "bat/bat_internal.h"

/*
 * BAT_$RESERVE
 *
 * Parameters:
 *   vol_idx - Volume index (0-6)
 *   count   - Number of blocks to reserve
 *   status  - Output status code
 *
 * Assembly analysis:
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Checks if enough free blocks exist (count + 0xB for old format volumes,
 *     just count for new format volumes)
 *   - Moves blocks from free_blocks to reserved_blocks
 *
 * The check for old format volumes requires an extra 0xB (11) blocks
 * to remain free, likely for system overhead.
 */
void BAT_$RESERVE(int16_t vol_idx, uint32_t count, status_$t *status)
{
    bat_$volume_t *vol;
    int8_t vol_flag;
    uint32_t required;

    ML_$LOCK(ML_LOCK_BAT);

    vol = &bat_$volumes[vol_idx];

    /*
     * Get volume type flag (byte 3 of volume flags).
     * For old format volumes (flag >= 0), require extra 0xB blocks.
     * For new format volumes (flag < 0), just check count.
     */
    vol_flag = (int8_t)((bat_$volume_flags[vol_idx] >> 24) & 0xFF);

    if (vol_flag >= 0) {
        /* Old format: need count + 0xB blocks available */
        required = count + 0xB;
        if (vol->free_blocks < required) {
            *status = status_$disk_is_full;
            goto done;
        }
    } else {
        /* New format: just need count blocks available */
        if (vol->free_blocks < count) {
            *status = status_$disk_is_full;
            goto done;
        }
    }

    /* Move blocks from free to reserved pool */
    vol->free_blocks -= count;
    vol->reserved_blocks += count;
    *status = status_$ok;

done:
    ML_$UNLOCK(ML_LOCK_BAT);
}
