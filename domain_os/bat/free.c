/*
 * BAT_$FREE - Free disk blocks
 *
 * Returns blocks to the volume's free block pool by setting bits
 * in the BAT bitmap.
 *
 * Original address: 0x00E3B516
 */

#include "bat/bat_internal.h"

/*
 * BAT_$FREE
 *
 * Parameters:
 *   blocks   - Array of block addresses to free
 *   count    - Number of blocks to free
 *   vol_idx  - Volume index (0-6)
 *   reserved - If non-zero, return blocks to reserved pool instead of free pool
 *   status   - Output status code
 *
 * Assembly analysis:
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Iterates through each block in the array
 *   - For block 0: handles as special case (reserved pool manipulation)
 *   - For other blocks: validates range, calculates bitmap position
 *   - Loads appropriate BAT bitmap block if not cached
 *   - Sets the corresponding bit to mark block as free
 *   - Updates partition free count based on partition index
 *   - Updates free_blocks or reserved_blocks counter
 */
void BAT_$FREE(uint32_t *blocks, int16_t count, int16_t vol_idx,
               int16_t reserved, status_$t *status)
{
    bat_$volume_t *vol;
    status_$t local_status;
    int16_t i;
    uint32_t block;
    uint32_t rel_block;     /* Block relative to first_data_block */
    uint32_t bat_block;     /* BAT bitmap block number */
    uint32_t word_offset;   /* Word offset within BAT block (0-255) */
    uint32_t bit_offset;    /* Bit offset within word (0-31) */
    uint32_t *bitmap_word;
    int16_t partition_idx;

    ML_$LOCK(ML_LOCK_BAT);

    local_status = status_$ok;
    *status = status_$ok;

    /* Check if volume is mounted */
    if (bat_$mounted[vol_idx] >= 0) {
        ML_$UNLOCK(ML_LOCK_BAT);
        *status = bat_$not_mounted;
        return;
    }

    vol = &bat_$volumes[vol_idx];

    /* Process each block in the array */
    for (i = count - 1; i >= 0; i--) {
        block = blocks[i];

        /* Handle block 0 specially - used for reserved pool management */
        if (block == 0) {
            if (reserved == 0) {
                /* Move one block from reserved to free */
                if (vol->reserved_blocks == 0) {
                    *status = bat_$error;
                } else {
                    vol->free_blocks++;
                    vol->reserved_blocks--;
                }
            }
            continue;
        }

        /* Calculate relative block number */
        rel_block = block - vol->first_data_block;

        /* Validate block is within valid range */
        if ((int32_t)rel_block < 0 || rel_block >= vol->total_blocks) {
            *status = bat_$invalid_block;
            continue;
        }

        /*
         * Calculate BAT bitmap position:
         * - Each BAT block covers 8192 blocks (256 words * 32 bits)
         * - bat_block = bat_block_start + (rel_block >> 13)
         * - word_offset = (rel_block >> 5) & 0xFF
         * - bit_offset = rel_block & 0x1F
         */
        bat_block = vol->bat_block_start + (rel_block >> 13);
        word_offset = (rel_block >> 5) & 0xFF;
        bit_offset = rel_block & 0x1F;

        /* Load BAT bitmap block if not already cached */
        if (bat_block != bat_$cached_block || bat_$cached_vol != vol_idx) {
            /* Flush current cached block if dirty */
            if (bat_$cached_buffer != NULL) {
                DBUF_$SET_BUFF(bat_$cached_buffer, bat_$cached_dirty, &local_status);
            }

            /* Load new BAT bitmap block */
            bat_$cached_buffer = DBUF_$GET_BLOCK(vol_idx, bat_block,
                                                  (void *)&BAT_$UID,
                                                  bat_block, 0, &local_status);
            if (local_status != status_$ok) {
                bat_$cached_buffer = NULL;
                bat_$cached_vol = 0;
                break;
            }

            bat_$cached_vol = vol_idx;
            bat_$cached_block = bat_block;
        }

        /* Mark buffer as dirty */
        bat_$cached_dirty = BAT_BUF_DIRTY;

        /* Get pointer to bitmap word */
        bitmap_word = (uint32_t *)bat_$cached_buffer + word_offset;

        /* Calculate partition index for this block */
        if (block < vol->partition_start_offset) {
            partition_idx = 0;
        } else {
            partition_idx = (int16_t)M$DIS$LLL(block - vol->partition_start_offset,
                                                vol->partition_size);
        }

        /* Check if block is already free (bit set = free) */
        if (*bitmap_word & (1U << bit_offset)) {
            *status = bat_$error;
            continue;
        }

        /* Set bit to mark block as free */
        *bitmap_word |= (1U << bit_offset);

        /* Update counters */
        if (reserved == 0) {
            vol->free_blocks++;
        } else {
            vol->reserved_blocks++;
        }

        /* Update partition free count */
        vol->partitions[partition_idx].free_count++;
    }

    ML_$UNLOCK(ML_LOCK_BAT);

    /* Propagate any error from buffer operations */
    if (local_status != status_$ok && *status == status_$ok) {
        *status = local_status;
    }
}
