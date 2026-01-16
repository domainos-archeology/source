/*
 * BAT_$ALLOCATE - Allocate disk blocks
 *
 * Allocates blocks from the volume's free block pool by searching the
 * BAT bitmap for free bits and clearing them.
 *
 * Original address: 0x00E3B0D6
 */

#include "bat/bat_internal.h"

/*
 * BAT_$ALLOCATE
 *
 * Parameters:
 *   vol_idx    - Volume index (0-6)
 *   hint       - Hint block number for locality
 *   count      - Number of blocks to allocate (low 16 bits) and flags (high 16 bits)
 *                High word: 0 = use free pool, non-0 = use reserved pool
 *   blocks_out - Output array receiving allocated block numbers
 *   status     - Output status code
 *
 * Assembly analysis (1088 bytes, complex allocation algorithm):
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Validates volume is mounted and enough blocks available
 *   - Calculates search ranges based on hint and partition structure
 *   - Uses two-phase search: first within allocation chunk, then wrap around
 *   - For each potential block, loads BAT bitmap block if needed
 *   - Clears bits for allocated blocks and updates partition counts
 *   - Updates free_blocks or reserved_blocks based on allocation type
 */
void BAT_$ALLOCATE(int16_t vol_idx, uint32_t hint, uint32_t count,
                   uint32_t *blocks_out, status_$t *status)
{
    bat_$volume_t *vol;
    int16_t alloc_count;        /* Number of blocks requested */
    int16_t use_reserved;       /* Use reserved pool flag */
    uint32_t rel_block;         /* Current block relative to first_data_block */
    uint32_t bat_block;         /* Current BAT bitmap block */
    uint32_t word_offset;       /* Word offset within BAT block */
    uint32_t bit_offset;        /* Bit offset within word */
    uint32_t bitmap_word;       /* Current bitmap word value */
    int16_t allocated;          /* Count of blocks allocated */
    int32_t step_remaining;     /* Steps remaining before skipping */
    int16_t partition_idx;      /* Current partition index */
    uint32_t partition_end;     /* End of current partition range */
    uint32_t chunk_end;         /* End of current allocation chunk */
    uint32_t chunk_start;       /* Start of current allocation chunk */
    uint32_t next_chunk_start;  /* Start of next chunk for wrap-around */
    int8_t phase;               /* Search phase (0 = in chunk, 1 = wrap) */
    uint32_t *out_ptr;          /* Output pointer */

    ML_$LOCK(ML_LOCK_BAT);

    /* Check if volume is mounted */
    if (bat_$mounted[vol_idx] >= 0) {
        *status = bat_$not_mounted;
        goto done;
    }

    vol = &bat_$volumes[vol_idx];

    /* Extract count and reserved flag from combined parameter */
    alloc_count = (int16_t)(count & 0xFFFF);
    use_reserved = (int16_t)((count >> 16) & 0xFFFF);

    /* Check if enough blocks available */
    if (use_reserved == 0) {
        /* Using free pool */
        int8_t vol_flag = (int8_t)((bat_$volume_flags[vol_idx] >> 24) & 0xFF);
        if (vol_flag >= 0) {
            /* Old format: need alloc_count + 0xB blocks */
            if ((int32_t)alloc_count > (int32_t)(vol->free_blocks - 0xB)) {
                *status = status_$disk_is_full;
                goto done;
            }
        } else {
            /* New format: just need alloc_count blocks */
            if ((int32_t)alloc_count > (int32_t)vol->free_blocks) {
                *status = status_$disk_is_full;
                goto done;
            }
        }
    } else {
        /* Using reserved pool */
        if ((int32_t)alloc_count > (int32_t)vol->reserved_blocks) {
            *status = status_$disk_is_full;
            goto done;
        }
    }

    /* Calculate starting block relative to first_data_block */
    if (hint < vol->first_data_block) {
        rel_block = 0;
    } else {
        rel_block = hint - vol->first_data_block;
    }

    /* Clamp to valid range */
    if (rel_block >= vol->total_blocks) {
        rel_block = vol->total_blocks - 1;
    }

    /* Initialize search state */
    step_remaining = vol->step_blocks - 1;
    allocated = 0;

    /* Calculate partition range containing hint block */
    {
        uint32_t part_base = vol->partition_size + vol->partition_start_offset;
        if (hint + vol->first_data_block < part_base) {
            partition_idx = 0;
            partition_end = part_base - vol->first_data_block;
            next_chunk_start = 0;
        } else {
            int32_t part_offset = M$DIS$LLL((hint + vol->first_data_block) -
                                             vol->partition_start_offset,
                                             vol->partition_size);
            partition_idx = (int16_t)part_offset;
            next_chunk_start = M$MIS$LLW(vol->partition_size, partition_idx);
            partition_end = vol->partition_size + next_chunk_start;
            next_chunk_start -= vol->first_data_block;
        }
    }

    /* Calculate allocation chunk range */
    if (rel_block < vol->alloc_chunk_offset) {
        chunk_start = 0;
        chunk_end = vol->alloc_chunk_offset;
    } else {
        int32_t chunk_idx = M$DIS$LLL(rel_block - vol->alloc_chunk_offset,
                                       vol->alloc_chunk_size);
        chunk_start = M$MIS$LLL(chunk_idx, vol->alloc_chunk_size);
        chunk_start += vol->alloc_chunk_offset;
        chunk_end = chunk_start + vol->alloc_chunk_size;
    }

    /* Clamp ranges to volume size */
    if (chunk_end > vol->total_blocks) {
        chunk_end = vol->total_blocks;
    }
    if (partition_end > vol->total_blocks) {
        partition_end = vol->total_blocks;
    }

    /* Calculate bitmap position */
    bat_block = vol->bat_block_start + (rel_block >> 13);
    word_offset = (rel_block >> 5) & 0xFF;
    bit_offset = rel_block & 0x1F;

    phase = -1;  /* Start in chunk search phase */

    /* Pre-load bitmap word if buffer cached */
    if (bat_$cached_buffer != NULL) {
        bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];
    }

    *status = status_$ok;
    out_ptr = blocks_out;

    /* Main allocation loop */
search_loop:
    /* Load BAT bitmap block if needed */
    if (bat_block != bat_$cached_block || bat_$cached_vol != vol_idx) {
        /* Flush current cached block */
        if (bat_$cached_buffer != NULL) {
            DBUF_$SET_BUFF(bat_$cached_buffer, bat_$cached_dirty, status);
        }

        /* Load new BAT bitmap block */
        bat_$cached_buffer = DBUF_$GET_BLOCK(vol_idx, bat_block,
                                              (void *)&BAT_$UID,
                                              bat_block, 0, status);
        if (*status != status_$ok) {
            bat_$cached_buffer = NULL;
            bat_$cached_vol = 0;
            goto done;
        }

        bat_$cached_vol = vol_idx;
        bat_$cached_dirty = BAT_BUF_CLEAN;
        bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];
        bat_$cached_block = bat_block;
    }

    /* Check if current word is all zeros (no free blocks) */
    if (bitmap_word == 0) {
        /* Skip to next word */
        rel_block += (32 - bit_offset);
        step_remaining -= (32 - bit_offset);
        bit_offset = 32;
    } else {
        /* Check if current bit is set (block is free) */
        if (bitmap_word & (1U << bit_offset)) {
            if (step_remaining < 1) {
                /* Allocate this block */
                bitmap_word &= ~(1U << bit_offset);
                ((uint32_t *)bat_$cached_buffer)[word_offset] = bitmap_word;
                bat_$cached_dirty = BAT_BUF_DIRTY;

                /* Record allocated block */
                *out_ptr = vol->first_data_block + rel_block;
                allocated++;
                out_ptr++;

                /* Update partition free count */
                vol->partitions[partition_idx].free_count--;

                /* Check if done */
                if (allocated >= alloc_count) {
                    goto allocation_done;
                }

                /* Reset step counter for next allocation */
                step_remaining = vol->step_blocks;
            } else {
                phase = -1;  /* Found free block, reset phase */
            }
        }

        /* Move to next bit */
        bit_offset++;
        rel_block++;
        step_remaining--;
    }

    /* Check if still within current chunk */
    if (rel_block < chunk_end) {
        /* Check if need to advance to next word */
        if (bit_offset >= 32) {
            word_offset++;
            if (word_offset >= 256) {
                bat_block++;
                word_offset = 0;
            } else {
                bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];
            }
            bit_offset -= 32;
        }
        goto search_loop;
    }

    /* End of chunk - check if need to switch phases or partitions */
    if (phase < 0) {
        /* Phase 0: Search in chunk completed, switch to wrap-around */
        phase = 0;
        rel_block = next_chunk_start;
    } else {
        /* Check if current partition is exhausted */
        if (rel_block >= partition_end) {
            /* Check if partition has any free blocks */
            if (vol->partitions[partition_idx].free_count == 0) {
                /* Move to next partition */
                partition_idx++;
                if (partition_idx < vol->num_partitions) {
                    next_chunk_start = partition_end;
                    partition_end += vol->partition_size;
                } else {
                    /* Wrap to first partition */
                    rel_block = 0;
                    partition_idx = 0;
                    next_chunk_start = 0;
                    partition_end = (vol->partition_size + vol->partition_start_offset) -
                                    vol->first_data_block;
                }
                next_chunk_start = rel_block;
                if (partition_end > vol->total_blocks) {
                    partition_end = vol->total_blocks;
                }
            }
        }

        /* Check if within allocation chunk offset range */
        if (rel_block < vol->alloc_chunk_offset) {
            chunk_start = 0;
            chunk_end = vol->alloc_chunk_offset;
            goto recalc_chunk;
        }

        /* Calculate next chunk */
        {
            int32_t chunk_idx = M$DIS$LLL(rel_block - vol->alloc_chunk_offset,
                                           vol->alloc_chunk_size);
            chunk_start = M$MIS$LLL(chunk_idx, vol->alloc_chunk_size);
            chunk_start += vol->alloc_chunk_offset;
        }

recalc_chunk:
        next_chunk_start = chunk_end;
        chunk_end = chunk_start + vol->alloc_chunk_size;
    }

    /* Clamp chunk end */
    if (chunk_end > vol->total_blocks) {
        chunk_end = vol->total_blocks;
    }

    /* Recalculate bitmap position */
    bat_block = vol->bat_block_start + (rel_block >> 13);
    word_offset = (rel_block >> 5) & 0xFF;
    bit_offset = rel_block & 0x1F;

    if (bat_block == bat_$cached_block) {
        bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];
    }

    goto search_loop;

allocation_done:
    /* Update free/reserved block count */
    if (use_reserved == 0) {
        vol->free_blocks -= allocated;
    } else {
        vol->reserved_blocks -= allocated;
    }

done:
    ML_$UNLOCK(ML_LOCK_BAT);
}
