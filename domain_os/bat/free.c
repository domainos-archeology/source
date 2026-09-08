/*
 * BAT_$FREE - return disk blocks to a volume's pool
 *
 * Original address: 0x00E3B516 (482 bytes, 0x00E3B516..0x00E3B6F7)
 *
 * Sets the bit for each block in the BAT bitmap ("free") and bumps the
 * volume and partition counters.
 */

#include "bat/bat_internal.h"

/*
 * BAT_$FREE
 *
 * Argument block (link.w A6,-0x34 at 0x00E3B516):
 *
 *   (0x08,A6)  long  blocks       - 0x00E3B582
 *   (0x0c,A6)  word  count        - 0x00E3B526
 *   (0x0e,A6)  word  vol_idx      - 0x00E3B540, 0x00E3B5EE, 0x00E3B624
 *   (0x10,A6)  word  reserved     - 0x00E3B594, 0x00E3B6A4
 *   (0x12,A6)  long  status       - 0x00E3B53A
 */
void BAT_$FREE(uint32_t *blocks, int16_t count, int16_t vol_idx,
               int16_t reserved, status_$t *status)
{
    bat_$volume_t *vol;
    status_$t local_status;         /* (-0x08,A6) */
    uint16_t  remaining;            /* (-0x2e,A6): the dbf-style counter */
    uint32_t *cursor;               /* (-0x34,A6) */
    uint32_t  block;                /* D3 */
    uint32_t  rel_block;            /* D0 */
    uint32_t  bat_block;            /* D2, then reused for the bitmap word */
    uint32_t  bitmap_word;          /* D2 after 0x00E3B662 */
    uint16_t  word_offset;          /* D6 */
    uint16_t  bit_offset;           /* D5 */
    int16_t   partition_idx;        /* D0w */

    ML_$LOCK(ML_LOCK_BAT);                          /* 0x00E3B52A */

    local_status = status_$ok;                      /* 0x00E3B536 */
    *status = status_$ok;                           /* 0x00E3B53E */

    /* 0x00E3B548..0x00E3B564: not mounted unlocks and returns at once. */
    if (bat_$mounted[vol_idx] >= 0) {
        ML_$UNLOCK(ML_LOCK_BAT);
        *status = bat_$not_mounted;
        return;
    }

    vol = &bat_$volumes[vol_idx];                   /* 0x00E3B568 */

    /*
     * 0x00E3B574..0x00E3B57A: `subq.w #0x1,D0w` then `bmi` - a count of
     * zero or less skips the loop entirely.
     */
    if ((int16_t)(count - 1) < 0) {
        goto unlock;
    }

    /*
     * 0x00E3B57E..0x00E3B588 set up a DESCENDING counter but an ASCENDING
     * cursor: 0x00E3B588 stores `blocks` itself and 0x00E3B6C2
     * `addq.l #0x4,(-0x34,A6)` walks it forward, so the array is visited
     * blocks[0] first.  The order is observable - the load-failure break at
     * 0x00E3B644 leaves the tail of the array untouched, and the BAT block
     * cache is primed by whichever block came first.
     */
    remaining = (uint16_t)(count - 1);
    cursor = blocks;

    for (;;) {                                      /* 0x00E3B58C */
        block = *cursor;                            /* 0x00E3B590 */

        if (block == 0) {
            /*
             * 0x00E3B594..0x00E3B5AC: a zero entry is not a block, it is a
             * request to hand one block back from the reserved pool to the
             * free pool.  Ignored outright when freeing into the reserved
             * pool.
             */
            if (reserved == 0) {
                if (vol->reserved_blocks == 0) {
                    *status = bat_$error;           /* 0x00E3B68A */
                } else {
                    vol->free_blocks++;
                    vol->reserved_blocks--;
                }
            }
            goto next_block;
        }

        /* 0x00E3B5B0..0x00E3B5C8 */
        rel_block = block - vol->first_data_block;
        if ((int32_t)rel_block < 0 || rel_block >= vol->total_blocks) {
            *status = bat_$invalid_block;
            goto next_block;
        }

        /* 0x00E3B5CC..0x00E3B5E0 */
        bit_offset  = (uint16_t)(rel_block & 0x1F);
        word_offset = (uint16_t)((rel_block >> 5) & 0xFF);
        bat_block   = vol->bat_block_start + (rel_block >> 13);

        /* 0x00E3B5E4..0x00E3B64E */
        if (bat_block != bat_$cached_block || bat_$cached_vol != vol_idx) {
            if (bat_$cached_buffer != NULL) {
                /* 0x00E3B5FC `pea (-0x8,A6)`: the shared local status. */
                DBUF_$SET_BUFF(bat_$cached_buffer, (uint16_t)bat_$cached_dirty,
                               &local_status);
            }

            bat_$cached_buffer = DBUF_$GET_BLOCK((uint16_t)vol_idx,
                                                 (int32_t)bat_block, &BAT_$UID,
                                                 bat_block, 0, &local_status);
            if (local_status != status_$ok) {       /* 0x00E3B636 */
                bat_$cached_buffer = NULL;
                bat_$cached_vol = 0;
                goto unlock;                        /* 0x00E3B644 */
            }

            bat_$cached_vol   = vol_idx;            /* 0x00E3B648 */
            bat_$cached_block = bat_block;          /* 0x00E3B64E */
        }

        bat_$cached_dirty = BAT_BUF_DIRTY;          /* 0x00E3B652 */

        /* 0x00E3B658..0x00E3B662 */
        bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];

        /*
         * 0x00E3B666..0x00E3B684: the partition index is derived from the
         * ABSOLUTE block, not from rel_block.
         */
        if (block < (uint32_t)vol->partition_start_offset) {
            partition_idx = 0;                      /* 0x00E3B670 */
        } else {
            partition_idx = (int16_t)M$DIS$LLL(
                (long)(block - (uint32_t)vol->partition_start_offset),
                (long)vol->partition_size);
        }

        /* 0x00E3B686: a set bit means the block was already free. */
        if ((bitmap_word & (1UL << (bit_offset & 0x1F))) != 0) {
            *status = bat_$error;                   /* 0x00E3B68A */
            goto next_block;
        }

        /* 0x00E3B696..0x00E3B6A0 */
        bitmap_word |= (1UL << (bit_offset & 0x1F));
        ((uint32_t *)bat_$cached_buffer)[word_offset] = bitmap_word;

        /* 0x00E3B6A4..0x00E3B6B0 */
        if (reserved == 0) {
            vol->free_blocks++;
        } else {
            vol->reserved_blocks++;
        }

        /* 0x00E3B6B4..0x00E3B6BE */
        vol->partitions[partition_idx].free_count++;

    next_block:                                     /* 0x00E3B6C2 */
        cursor++;
        if (remaining-- == 0) {                     /* 0x00E3B6C6 subq/bcc */
            break;
        }
    }

unlock:                                             /* 0x00E3B6CE */
    ML_$UNLOCK(ML_LOCK_BAT);

    /*
     * 0x00E3B6DC..0x00E3B6EA: a buffer error is reported only when the
     * caller's status is still clean.
     */
    if (local_status != status_$ok && *status == status_$ok) {
        *status = local_status;
    }
}
