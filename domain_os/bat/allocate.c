/*
 * BAT_$ALLOCATE - allocate disk blocks from a volume's BAT bitmap
 *
 * Original address: 0x00E3B0D6 (1088 bytes, 0x00E3B0D6..0x00E3B515)
 *
 * The routine walks the BAT bitmap looking for set bits (a set bit means
 * "block free") and clears the ones it hands back.  It searches in three
 * nested extents, from the inside out:
 *
 *   allocation chunk   [chunk_start, chunk_end)      - one track
 *   partition          [partition_start, partition_end)
 *   volume             [0, total_blocks)
 *
 * plus a stride: `step_remaining` counts down and only a bit reached with
 * the counter at or below zero is actually taken, which spreads
 * consecutive allocations `step_blocks` apart.
 */

#include "bat/bat_internal.h"

/*
 * BAT_$ALLOCATE
 *
 * Argument block (link.w A6,-0x44 at 0x00E3B0D6):
 *
 *   (0x08,A6)  word  vol_idx
 *   (0x0a,A6)  long  hint
 *   (0x0e,A6)  word  alloc_count     - 0x00E3B134, 0x00E3B156, 0x00E3B38E
 *   (0x10,A6)  word  use_reserved    - 0x00E3B120, 0x00E3B4EC
 *   (0x12,A6)  long  blocks_out      - 0x00E3B28A
 *   (0x16,A6)  long  status          - 0x00E3B104, 0x00E3B284
 *
 * The two words at 0x0e and 0x10 are SEPARATE 16-bit parameters, not the
 * halves of one longword: the image tests 0x10 on its own at 0x00E3B120
 * (`tst.w`) and compares 0x0e on its own at 0x00E3B38E (`cmp.w`).  Every
 * call site pushes them as one `move.l` immediate only because both fit
 * in one instruction; on big-endian m68k the high half of that longword
 * lands at 0x0e (alloc_count) and the low half at 0x10 (use_reserved).
 */
void BAT_$ALLOCATE(int16_t vol_idx, uint32_t hint, int16_t alloc_count,
                   int16_t use_reserved, uint32_t *blocks_out,
                   status_$t *status)
{
    bat_$volume_t *vol;

    /* Frame cells, named by their (disp,A6) slot. */
    status_$t   set_status;         /* (-0x04,A6): DBUF_$SET_BUFF's own status */
    uint32_t    bitmap_word = 0;    /* (-0x08,A6) */
    int32_t     step_remaining;     /* (-0x0c,A6) */
    uint32_t    partition_end;      /* (-0x10,A6) */
    uint32_t    partition_start;    /* (-0x14,A6) */
    uint32_t    chunk_end;          /* (-0x18,A6) */
    int16_t     allocated;          /* (-0x26,A6) */
    int16_t     partition_idx;      /* (-0x2c,A6) */
    int8_t      rescan_chunk;       /* (-0x2e,A6) */
    uint32_t   *out_ptr;            /* (-0x40,A6) */

    /* Register-resident state. */
    uint32_t    rel_block;          /* D3: block relative to first_data_block */
    uint16_t    word_offset;        /* D4: longword index inside the BAT block */
    uint32_t    bat_block;          /* D5: BAT bitmap block number */
    uint32_t    chunk_start;        /* D6 */
    uint16_t    bit_offset;         /* D2w */

    ML_$LOCK(ML_LOCK_BAT);                          /* 0x00E3B0EA */

    /* 0x00E3B0FE..0x00E3B10E */
    if (bat_$mounted[vol_idx] >= 0) {
        *status = bat_$not_mounted;
        goto done;
    }

    vol = &bat_$volumes[vol_idx];                   /* 0x00E3B112 */

    /*
     * 0x00E3B120..0x00E3B16C: is the requested pool big enough?
     *
     * The image evaluates both free-pool tests and picks between them with
     * the volume's format flag:
     *
     *   0x00E3B13E  sgt D1b                 ; alloc_count > free_blocks - 0xB
     *   0x00E3B140  move.b (0xd3f,A0),D3b   ; bat_$volume_flags[vol_idx]
     *   0x00E3B144  not.b D3b
     *   0x00E3B146  and.b D3b,D1b
     *   0x00E3B148  bmi.b 0x00e3b162        ; old format and short: full
     *   0x00E3B14A  tst.b (0xd3f,A0)
     *   0x00E3B14E  bpl.b 0x00e3b170        ; old format and long enough: go
     *   0x00E3B150  cmp.l (-0x230,A2),D0    ; new format: the plain test
     *
     * so an old-format volume keeps a 0xB-block cushion and a new-format
     * one does not.  Both comparisons are signed (`sgt` / `ble`) on the
     * sign-extended alloc_count word.
     */
    if (use_reserved != 0) {
        if ((int32_t)alloc_count > (int32_t)vol->reserved_blocks) {
            *status = status_$disk_is_full;
            goto done;
        }
    } else if (bat_$volume_flags[vol_idx] >= 0) {
        if ((int32_t)alloc_count > (int32_t)(vol->free_blocks - 0xB)) {
            *status = status_$disk_is_full;
            goto done;
        }
    } else {
        if ((int32_t)alloc_count > (int32_t)vol->free_blocks) {
            *status = status_$disk_is_full;
            goto done;
        }
    }

    /* 0x00E3B170..0x00E3B18A: clamp the hint into [0, total_blocks). */
    if (hint < vol->first_data_block) {
        rel_block = 0;
    } else {
        rel_block = hint - vol->first_data_block;
    }
    if (rel_block >= vol->total_blocks) {
        rel_block = vol->total_blocks - 1;
    }

    /* 0x00E3B18C..0x00E3B196 */
    step_remaining = (int32_t)BAT_STEP_LONG(vol) - 1;
    allocated = 0;

    /*
     * 0x00E3B19A..0x00E3B1FA: locate the partition holding the block.
     *
     * 0x00E3B19A `move.l D3,D2` reloads D2 from the CLAMPED relative block
     * and 0x00E3B1A2 adds first_data_block back, so the partition search
     * uses the clamped absolute block, not the raw hint the caller passed.
     */
    {
        uint32_t abs_block = rel_block + vol->first_data_block;
        uint32_t pso = (uint32_t)vol->partition_start_offset;
        uint32_t first_end = pso + vol->partition_size;

        if (abs_block < first_end) {
            /* 0x00E3B1B4..0x00E3B1C4 */
            partition_idx = 0;
            partition_start = 0;
            partition_end = first_end - vol->first_data_block;
        } else {
            /* 0x00E3B1C6..0x00E3B1FA */
            partition_idx = (int16_t)M$DIS$LLL((long)(abs_block - pso),
                                               (long)vol->partition_size);
            partition_start = (pso + (uint32_t)M$MIS$LLW(
                                          (long)vol->partition_size,
                                          (short)partition_idx))
                              - vol->first_data_block;
            partition_end = partition_start + vol->partition_size;
        }
    }

    /* 0x00E3B1FC..0x00E3B23A: locate the allocation chunk holding it. */
    if (rel_block < vol->alloc_chunk_offset) {
        chunk_start = 0;
        chunk_end = vol->alloc_chunk_offset;
    } else {
        chunk_start = (uint32_t)M$MIS$LLL(
                          M$DIS$LLL((long)(rel_block - vol->alloc_chunk_offset),
                                    (long)vol->alloc_chunk_size),
                          (long)vol->alloc_chunk_size)
                      + vol->alloc_chunk_offset;
        chunk_end = chunk_start + vol->alloc_chunk_size;
    }

    /* 0x00E3B23C..0x00E3B252: neither extent may run past the volume. */
    if (vol->total_blocks < chunk_end) {
        chunk_end = vol->total_blocks;
    }
    if (vol->total_blocks < partition_end) {
        partition_end = vol->total_blocks;
    }

    /*
     * 0x00E3B254..0x00E3B292: bitmap cursor.  Each BAT block holds 0x100
     * longwords = 0x2000 bits, hence the >> 13 / >> 5 / & 0x1F split.
     */
    bat_block   = vol->bat_block_start + (rel_block >> 13);
    word_offset = (uint16_t)((rel_block >> 5) & 0xFF);
    bit_offset  = (uint16_t)(rel_block & 0x1F);

    rescan_chunk = -1;                              /* 0x00E3B26C st */

    /*
     * 0x00E3B270..0x00E3B282.  The image leaves (-0x08,A6) undefined when
     * no buffer is cached; the initialiser above stands in for that slot
     * and is only observable on a path the loop head immediately reloads.
     */
    if (bat_$cached_buffer != NULL) {
        bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];
    }

    *status = status_$ok;                           /* 0x00E3B288 */
    out_ptr = blocks_out;                           /* 0x00E3B290 */

search_loop:                                        /* 0x00E3B294 */
    if (bat_block != bat_$cached_block || bat_$cached_vol != vol_idx) {
        if (bat_$cached_buffer != NULL) {           /* 0x00E3B2A4 */
            /*
             * 0x00E3B2AC `pea (-0x4,A6)` - the release status goes to a
             * frame cell of its own, NOT to the caller's status word.
             */
            DBUF_$SET_BUFF(bat_$cached_buffer, (uint16_t)bat_$cached_dirty,
                           &set_status);
        }

        /* 0x00E3B2C2..0x00E3B2E2 */
        /*
         * 0x00E3B2C8 pushes one `clr.l`, which DBUF_$GET_BLOCK reads back as
         * the two WORDS at its (0x16,A6) and (0x18,A6) (0x00E3A5CE /
         * 0x00E3A5D2) - block_type 0 and flags 0.
         */
        bat_$cached_buffer = DBUF_$GET_BLOCK((uint16_t)vol_idx,
                                             (int32_t)bat_block, &BAT_$UID,
                                             bat_block, 0, 0, status);
        if (*status != status_$ok) {                /* 0x00E3B2EA */
            bat_$cached_buffer = NULL;
            bat_$cached_vol = 0;
            goto done;
        }

        bat_$cached_vol   = vol_idx;                /* 0x00E3B2FA */
        bat_$cached_block = bat_block;              /* 0x00E3B300 */
        bat_$cached_dirty = BAT_BUF_CLEAN;          /* 0x00E3B304 */
        bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];
    }

    if (bitmap_word == 0) {
        /*
         * 0x00E3B31E..0x00E3B334: no free block in this longword, skip the
         * rest of it in one go and jump straight to the extent tests.
         */
        uint32_t skip = 0x20u - (uint32_t)bit_offset;

        rel_block += skip;
        step_remaining -= (int32_t)skip;
        bit_offset = 0x20;
    } else {
        /* 0x00E3B33A `btst.l D2,D0` tests bit (bit_offset mod 32). */
        if ((bitmap_word & (1UL << (bit_offset & 0x1F))) != 0) {
            if (step_remaining > 0) {               /* 0x00E3B33E bgt */
                /*
                 * 0x00E3B39E: a free block was passed over because of the
                 * stride, so the chunk is worth a second sweep.
                 */
                rescan_chunk = -1;
            } else {
                /* 0x00E3B344..0x00E3B35C: take the block. */
                bitmap_word &= ~(1UL << (bit_offset & 0x1F));
                ((uint32_t *)bat_$cached_buffer)[word_offset] = bitmap_word;
                bat_$cached_dirty = BAT_BUF_DIRTY;

                /* 0x00E3B362..0x00E3B372 */
                *out_ptr = vol->first_data_block + rel_block;
                allocated++;
                out_ptr++;

                /* 0x00E3B376..0x00E3B386 */
                vol->partitions[partition_idx].free_count--;

                if (allocated >= alloc_count) {     /* 0x00E3B38E bge */
                    goto allocation_done;
                }

                /* 0x00E3B396: rearm the stride for the next block. */
                step_remaining = (int32_t)BAT_STEP_LONG(vol);
            }
        }

        /* 0x00E3B3A2..0x00E3B3A6 */
        bit_offset++;
        rel_block++;
        step_remaining--;
    }

    /* 0x00E3B3AA: still inside the allocation chunk? */
    if (rel_block < chunk_end) {
        /* 0x00E3B4C0..0x00E3B4E8: step the bitmap cursor to the next word. */
        if (bit_offset >= 0x20) {
            word_offset++;
            if (word_offset == 0x100) {             /* 0x00E3B4CA */
                bat_block++;
                word_offset = 0;
            } else {
                bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];
            }
            bit_offset -= 0x20;
        }
        goto search_loop;
    }

    /* 0x00E3B3B2: the chunk is exhausted. */
    if (rescan_chunk < 0) {
        /*
         * 0x00E3B3B8 `move.l D6,D3` restarts at the chunk's own START, not
         * at any partition or next-chunk boundary, and clears the flag so
         * the restart happens only once per free block passed over.
         */
        rel_block = chunk_start;
        rescan_chunk = 0;
        goto clamp_chunk_end;                       /* 0x00E3B3BE */
    }

    if (rel_block >= partition_end) {               /* 0x00E3B3C2 bcs */
        /* 0x00E3B3CA..0x00E3B3DA */
        if (vol->partitions[partition_idx].free_count != 0) {
            /*
             * 0x00E3B3E0: the partition still reports free blocks, so sweep
             * it again from its start rather than moving on.
             */
            rel_block = partition_start;
        } else {
            partition_idx++;                        /* 0x00E3B3E6 */

            if ((int32_t)partition_idx < (int32_t)vol->num_partitions) {
                /* 0x00E3B414..0x00E3B420 */
                partition_start = partition_end;
                partition_end = partition_start + vol->partition_size;
            } else {
                /* 0x00E3B3FA..0x00E3B412: wrap to the first partition. */
                rel_block = 0;
                partition_idx = 0;
                partition_start = 0;
                partition_end = ((uint32_t)vol->partition_start_offset +
                                 vol->partition_size) - vol->first_data_block;
            }

            /* 0x00E3B426..0x00E3B430 */
            if (partition_end > vol->total_blocks) {
                partition_end = vol->total_blocks;
            }
        }

        /* 0x00E3B432: pick the chunk containing the new block. */
        if (rel_block < vol->alloc_chunk_offset) {
            chunk_start = 0;                        /* 0x00E3B468 */
            chunk_end = vol->alloc_chunk_offset;
        } else {
            chunk_start = (uint32_t)M$MIS$LLL(
                              M$DIS$LLL(
                                  (long)(rel_block - vol->alloc_chunk_offset),
                                  (long)vol->alloc_chunk_size),
                              (long)vol->alloc_chunk_size)
                          + vol->alloc_chunk_offset;
            chunk_end = chunk_start + vol->alloc_chunk_size;  /* 0x00E3B476 */
        }
    } else if (rel_block >= vol->total_blocks) {    /* 0x00E3B462 bcs */
        chunk_start = 0;                            /* 0x00E3B468 */
        chunk_end = vol->alloc_chunk_offset;
    } else {
        /*
         * 0x00E3B472: inside the partition the next chunk simply begins at
         * the old chunk's end - no division, and chunk_end is recomputed
         * from that start at 0x00E3B476.
         */
        chunk_start = chunk_end;
        chunk_end = chunk_start + vol->alloc_chunk_size;
    }

clamp_chunk_end:                                    /* 0x00E3B480 */
    if (vol->total_blocks < chunk_end) {
        chunk_end = vol->total_blocks;
    }

    /* 0x00E3B48E..0x00E3B4BC */
    bat_block   = vol->bat_block_start + (rel_block >> 13);
    word_offset = (uint16_t)((rel_block >> 5) & 0xFF);
    bit_offset  = (uint16_t)(rel_block & 0x1F);

    if (bat_block == bat_$cached_block) {
        bitmap_word = ((uint32_t *)bat_$cached_buffer)[word_offset];
    }
    goto search_loop;

allocation_done:                                    /* 0x00E3B4EC */
    /* `ext.l D1` on the allocated-count word, so the subtraction is signed. */
    if (use_reserved != 0) {
        vol->reserved_blocks -= (uint32_t)(int32_t)allocated;
    } else {
        vol->free_blocks -= (uint32_t)(int32_t)allocated;
    }

done:                                               /* 0x00E3B500 */
    ML_$UNLOCK(ML_LOCK_BAT);
}
