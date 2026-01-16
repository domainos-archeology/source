/*
 * BAT_$ALLOC_VTOCE - Allocate a VTOCE block
 *
 * Allocates a Volume Table of Contents Entry block for file metadata.
 * May create a new VTOCE block if existing ones are full.
 *
 * Original address: 0x00E3AEC0
 */

#include "bat/bat_internal.h"

/*
 * BAT_$ALLOC_VTOCE
 *
 * Parameters:
 *   vol_idx   - Volume index (0-6)
 *   hint      - Hint block for locality (or 0 for any)
 *   block_out - Output receiving allocated VTOCE block number
 *   status    - Output status code
 *   new_vtoce - Output: non-zero if a new VTOCE block was allocated
 *
 * Assembly analysis:
 *   - Sets new_vtoce to 0 initially
 *   - Takes ML_LOCK_BAT for thread safety
 *   - If hint is provided, calculates partition index
 *   - Validates hint partition has enough free blocks (> partition_size/8)
 *   - If no valid hint, searches partitions alternating from middle
 *   - Two partition types:
 *     - Type 2: Has VTOCE with free entries
 *     - Other: Regular partition
 *   - If partition has existing VTOCE, uses it
 *   - Otherwise allocates new VTOCE block
 *   - Initializes new VTOCE block with magic number and entry count
 *   - Updates partition VTOCE chain
 */
void BAT_$ALLOC_VTOCE(int16_t vol_idx, uint32_t hint, uint32_t *block_out,
                      status_$t *status, int8_t *new_vtoce)
{
    bat_$volume_t *vol;
    bat_$partition_t *part;
    bat_$vtoce_block_t *vtoce;
    int16_t partition_idx;
    uint32_t partition_size;
    uint32_t threshold;
    uint32_t vtoce_block;
    uint16_t alloc_flags;
    int16_t remaining;
    int16_t search_idx;
    int16_t direction;
    int32_t best_free;

    *new_vtoce = 0;

    ML_$LOCK(ML_LOCK_BAT);

    vol = &bat_$volumes[vol_idx];
    partition_size = vol->partition_size;
    threshold = partition_size >> 3;  /* 1/8 of partition size */

    partition_idx = -1;  /* Invalid initially */

    /* If hint provided, try to use its partition */
    if (hint != 0) {
        if (hint < vol->partition_start_offset) {
            partition_idx = 0;
        } else {
            partition_idx = (int16_t)M$DIS$LLL(hint - vol->partition_start_offset,
                                                partition_size);
        }

        /* Validate partition index and free space */
        if (partition_idx >= vol->num_partitions ||
            vol->partitions[partition_idx].free_count <= threshold) {
            partition_idx = -1;  /* Invalid, will search */
        }
    }

    /* If no valid partition from hint, search all partitions */
    if (partition_idx == -1) {
        /* Start from middle, alternating direction */
        search_idx = vol->num_partitions >> 1;
        best_free = -1;
        direction = 1;

        remaining = vol->num_partitions - 1;
        while (remaining >= 0) {
            part = &vol->partitions[search_idx];

            /* Check for type 2 partition (has VTOCE with free entries) */
            if (part->status == 2 && part->free_count > threshold) {
                partition_idx = search_idx;
                break;
            }

            /* Track best partition by free count */
            if ((int32_t)part->free_count > best_free) {
                partition_idx = search_idx;
                best_free = part->free_count;
            }

            /* Alternate search direction: +1, -1, +2, -2, +3, -3, ... */
            if (direction & 1) {
                search_idx -= direction;
            } else {
                search_idx += direction;
            }
            direction++;
            remaining--;
        }

        /* If no partition found, use partition 0 */
        if (partition_idx == 0) {
            hint = 0;
        } else if (partition_idx > 0) {
            /* Calculate hint block from partition index */
            hint = M$MIS$LLW(partition_size, partition_idx);
            hint += vol->partition_start_offset;
        }
    }

    part = &vol->partitions[partition_idx];

    /* Check if partition has existing VTOCE */
    vtoce_block = BAT_GET_VTOCE_BLOCK(part);

    if (vtoce_block == 0) {
        /* Need to allocate new VTOCE block */
        ML_$UNLOCK(ML_LOCK_BAT);
        BAT_$ALLOCATE(vol_idx, hint, 0x10000, block_out, status);
        ML_$LOCK(ML_LOCK_BAT);

        if (*status != status_$ok) {
            goto done;
        }

        alloc_flags = 0x10;  /* New block flag */
    } else {
        /* Use existing VTOCE block */
        *block_out = vtoce_block;
        alloc_flags = 0;
    }

    /* Get VTOCE block into buffer */
    vtoce = (bat_$vtoce_block_t *)DBUF_$GET_BLOCK(vol_idx, *block_out,
                                                   (void *)&VTOC_$UID,
                                                   *block_out, alloc_flags, status);
    if (*status != status_$ok) {
        goto done;
    }

    /* Initialize new VTOCE block if allocated */
    if (alloc_flags == 0x10) {
        int16_t i;
        uint32_t *ptr = (uint32_t *)vtoce;

        /* Zero the entire block */
        for (i = 0; i <= 0xFD; i++) {
            *ptr++ = 0;
        }

        /* Set magic number and self-reference */
        vtoce->magic = VTOCE_MAGIC;
        vtoce->self_block = *block_out;

        *new_vtoce = -1;  /* True (0xFF) */

        /* Update partition VTOCE chain */
        BAT_SET_VTOCE_BLOCK(part, *block_out);
    }

    /* Increment entry count in VTOCE */
    vtoce->entry_count++;

    /* If VTOCE is full (3 entries), update chain to next VTOCE */
    if (vtoce->entry_count == VTOCE_ENTRIES_PER_BLOCK) {
        /* Move next_vtoce to partition chain head */
        BAT_SET_VTOCE_BLOCK(part, vtoce->next_vtoce);
    }

done:
    ML_$UNLOCK(ML_LOCK_BAT);
}
