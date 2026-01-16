/*
 * BAT_$ALLOC_FM - Allocate block using First Match algorithm
 *
 * Allocates a single block by searching partitions for available space.
 * Uses a first-match strategy starting from the middle partition.
 *
 * Original address: 0x00E3AD20
 */

#include "bat/bat_internal.h"

/*
 * BAT_$ALLOC_FM
 *
 * Parameters:
 *   vol_idx - Volume index (0-6)
 *   status  - Output status code
 *
 * Returns:
 *   Allocated block number, or 0 on failure
 *
 * Assembly analysis:
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Searches partitions starting from the middle (num_partitions / 2)
 *   - For each partition, checks if it has enough free blocks
 *   - Two types of partitions:
 *     - Type 1 (status=1): Active partition with VTOCE, needs > partition_size/4 free
 *     - Type 0: Regular partition, uses half of free_count as metric
 *   - Selects partition with best free block count
 *   - Calculates block hint from partition index
 *   - Calls BAT_$ALLOCATE to allocate a single block
 */
uint32_t BAT_$ALLOC_FM(int16_t vol_idx, status_$t *status)
{
    bat_$volume_t *vol;
    int16_t num_parts;
    int16_t search_idx;
    int16_t best_idx;
    uint32_t best_count;
    uint32_t threshold;
    int16_t remaining;
    uint32_t hint;
    uint32_t block;

    ML_$LOCK(ML_LOCK_BAT);

    *status = status_$ok;
    vol = &bat_$volumes[vol_idx];

    /* Start search from middle partition */
    num_parts = vol->num_partitions;
    search_idx = num_parts >> 1;
    best_idx = 0;
    best_count = 0;

    /* Search all partitions */
    remaining = num_parts - 1;
    threshold = vol->partition_size >> 2;  /* Quarter of partition size */

    while (remaining >= 0) {
        bat_$partition_t *part = &vol->partitions[search_idx];
        uint32_t part_free = part->free_count;

        if (part->status == 1) {
            /* Type 1: Active partition with VTOCE chain */
            /* If has more than threshold free blocks, use immediately */
            if (part_free > threshold) {
                best_idx = search_idx;
                break;
            }
            /* Otherwise, track as candidate if better than current best */
            if ((int32_t)best_count < (int32_t)part_free) {
                best_count = part_free;
                best_idx = search_idx;
            }
        } else {
            /* Type 0: Regular partition - use half of free count as metric */
            uint32_t metric = part_free >> 1;
            if ((int32_t)best_count < (int32_t)metric) {
                best_count = metric;
                best_idx = search_idx;
            }
        }

        /* Move to next partition (wrapping) */
        search_idx++;
        if (search_idx >= num_parts) {
            search_idx = 0;
        }
        remaining--;
    }

    /* Calculate hint block from partition index */
    if (best_idx == 0) {
        hint = 0;
    } else {
        hint = M$MIS$LLW(vol->partition_size, best_idx);
        hint += vol->partition_start_offset;
    }

    ML_$UNLOCK(ML_LOCK_BAT);

    /* Allocate one block using the hint */
    BAT_$ALLOCATE(vol_idx, hint, 0x10000, &block, status);

    return block;
}
