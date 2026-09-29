/*
 * AST_$GET_SEG_MAP - Build a bitmap of the pages an object has
 *
 * `output` is eight longwords (256 bits).  For each segment of the object
 * touched by [start_offset, start_offset + map_size*1KB) - or just the
 * first segment when map_size <= 0x20 - the routine activates the AOTE,
 * finds the ASTE (creating it after a VTOCE_$LOOKUP_FM for a local
 * object), and under the PMAP lock examines every page of that segment
 * that lies inside the object: a page counts as PRESENT when it is
 * installed and either flags bit 0 is set, or its MMAPE's disk address
 * has bit 22 clear, or the MMAPE is modified, or its PFT word is
 * modified; a non-installed page counts when it has a disk address and
 * either flags bit 0 is set or the entry's bit 22 is clear.  A present
 * page sets bit ((offset mod seg_bytes) / 1KB / seg_count) of output
 * word (offset / seg_bytes), where seg_bytes = seg_count << 15.
 *
 * For a remote object the whole map comes from REM_FILE_$GET_SEG_MAP
 * instead (its eight longwords are copied, or zeroed on failure).  For a
 * local object with seg_count == 0 the map is post-processed by a bit
 * expansion loop (0x00E06EF8..0x00E06F66) that the image can never
 * complete: it divides by the low word of seg_count, which is zero.
 *
 * Parameters (frame at 0x00E06B1E, `link.w A6,-0x7c`):
 *   uid          (0x08,A6)
 *   start_offset (0x0C,A6)  longword
 *   location     (0x10,A6)  longword; cleared (`clr.l (0x10,A6)`) and
 *                           handed to ast_$force_activate_segment
 *   seg_count    (0x14,A6)  longword VALUE (`pea (0x1).w` at callers)
 *   map_size     (0x18,A6)  longword VALUE, in 1KB pages
 *   flags        (0x1C,A6)  word; bit 0 of its low byte ((0x1d,A6))
 *   output       (0x1E,A6)  eight longwords
 *   status       (0x22,A6)
 * Frame cells: (-0x20) the remote reply, (-0x28) net/node pair from
 * aote+0xAC, (-0x2C) aote->length, (-0x34) start segment, (-0x40)
 * seg_bytes, (-0x44) aligned start, (-0x4C)/(-0x50) LOOKUP_FM outputs,
 * (-0x5E) bit index, (-0x62) current segment, (-0x68) is_remote, (-0x6A)
 * segments left, (-0x74) segment byte offset, (-0x78) output, (-0x7C) the
 * one-bit mask.
 *
 * Original address: 0x00E06B1E (1130 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "vtoc/vtoc.h"
#include "rem_file/rem_file.h"
#include "math/math.h"

/*
 * 0x00E06E44..0x00E06E56 and 0x00E06F36..0x00E06F46: clear a longword,
 * then `moveq #0x1f / sub.w bit / bcs` and `bset.b bit,(byte (31-bit)>>3)`
 * - on this big-endian longword that is bit `bit`, and nothing for a bit
 * number above 31.
 */
static uint32_t ast_$seg_map_bit(uint16_t bit)
{
    if (bit > 0x1F) {
        return 0;
    }
    return 1u << bit;
}

void AST_$GET_SEG_MAP(uid_t *uid, uint32_t start_offset, uint32_t location,
                      uint32_t seg_count, uint32_t map_size, uint16_t flags,
                      uint32_t *output, status_$t *status)
{
    uint32_t seg_bytes;             /* (-0x40,A6) */
    uint32_t aligned_start;         /* (-0x44,A6) */
    uint32_t start_seg;             /* (-0x34,A6) */
    uint32_t end_seg;               /* D0 */
    uint16_t segs_left;             /* (-0x6A,A6) */
    uint16_t seg;                   /* (-0x62,A6) */
    uint32_t seg_off;               /* (-0x74,A6) */
    uint32_t length;                /* (-0x2C,A6) */
    uint32_t net_node[2];           /* (-0x28,A6) */
    int8_t is_remote;               /* (-0x68,A6) */
    uint32_t fm_block;              /* (-0x50,A6) */
    uint32_t fm_count;              /* (-0x4C,A6) */
    uint32_t reply[8];              /* (-0x20,A6) */
    uint32_t mask;                  /* (-0x7C,A6) */
    aote_t *aote;                   /* A2 */
    aste_t *aste;                   /* A3 */
    uint32_t *entry;                /* A2 in the page loop */
    uint32_t first_page;            /* D1 */
    uint32_t last_page;             /* D2 */
    uint32_t rel_off;               /* D4 */
    int32_t pages_in_seg;           /* D2 */
    int16_t pages_left;             /* D3w */
    int8_t present;                 /* D2b */
    uint16_t word_idx;              /* D1w */
    uint16_t bit;                   /* D0w */
    mmape_t *mmape;
    uint32_t *pft;
    int16_t i;
    uint16_t b;
    uint16_t run_start;             /* D3w */
    int16_t run_left;               /* D1w */
    uint32_t q;                     /* D1 */

    /* 0x00E06B2C..0x00E06B40 */
    *status = status_$ok;
    for (i = 0x7; i >= 0; i--) {
        output[i] = 0;
    }

    /* 0x00E06B44..0x00E06B62: seg_bytes = seg_count << 15; the aligned
     * start clears only bits 9..0 of the LOW word (andi.w #-0x400) */
    seg_bytes = seg_count << 15;
    aligned_start = start_offset & 0xFFFFFC00u;
    start_seg = aligned_start >> 15;

    /* 0x00E06B66..0x00E06B82: one segment for a small map, else
     * start + ((map_size << 10) >> 15) - 1 */
    if (map_size <= 0x20) {
        end_seg = start_seg;
    } else {
        end_seg = ((map_size << 10) >> 15) + start_seg - 1;
    }

    /* 0x00E06B84..0x00E06B9C: 16-bit range check and counter */
    if ((uint16_t)end_seg < (uint16_t)start_seg) {
        return;
    }
    segs_left = (uint16_t)((uint16_t)end_seg - (uint16_t)start_seg);
    seg = (uint16_t)start_seg;
    seg_off = (uint32_t)seg << 15;

    do {
        /* 0x00E06BA0..0x00E06BB2 */
        PROC1_$INHIBIT_BEGIN();
        ML_$LOCK(AST_LOCK_ID);

        /* 0x00E06BB4..0x00E06BC4 */
        aote = ast_$lookup_aote_by_uid(uid);
        if (aote == NULL) {
            /* 0x00E06BC6..0x00E06BEA: location := 0, force := 0 */
            location = 0;
            aote = ast_$force_activate_segment(uid, location, status, 0);
            if (aote == NULL) {
                goto unlock_and_return;
            }
        } else {
            /* 0x00E06BEE */
            aote->flags |= AOTE_FLAG_BUSY;
        }

        /* 0x00E06BF4 */
        length = aote->length;

        /* 0x00E06BFA..0x00E06C18 */
        aste = ast_$lookup_aste(aote, (int16_t)seg);
        if (aste == NULL && aote->remote_flag >= 0) {
            /*
             * 0x00E06C1C..0x00E06C48: a local object with no ASTE for this
             * segment - hold the AOTE, drop the lock and ask the VTOC for
             * the segment's file map (block seg, flags 0).
             */
            aote->ref_count++;
            ML_$UNLOCK(AST_LOCK_ID);
            VTOCE_$LOOKUP_FM(&aote->obj_uid, seg, 0, &fm_block, &fm_count,
                             status);
            /* 0x00E06C4C..0x00E06C68: "no file map" is not an error; any
             * other failure is made fatal (bit 31) - either way the
             * routine ends without retaking the lock */
            if (*status != status_$ok) {
                if (*status == 0x20003) {
                    *status = status_$ok;
                } else {
                    *status |= (status_$t)0x80000000u;
                }
                aote->ref_count--;
                goto inhibit_end_and_return;
            }
            /* 0x00E06C6A..0x00E06CA8 */
            ML_$LOCK(AST_LOCK_ID);
            aote->ref_count--;
            aste = ast_$lookup_aste(aote, (int16_t)seg);
            if (aste == NULL) {
                aste = ast_$lookup_or_create_aste(aote, seg, status);
                if (aste == NULL) {
                    goto unlock_and_return;
                }
            }
        }

        /* 0x00E06CC2..0x00E06CD4: aote+0xAC/+0xB0 and the remote flag */
        net_node[0] = aote->obj_loc_net;
        net_node[1] = aote->obj_loc_node;
        is_remote = (aote->remote_flag < 0) ? -1 : 0;

        /* 0x00E06CD8: no ASTE (remote) - just drop the lock */
        if (aste == NULL) {
            ML_$UNLOCK(AST_LOCK_ID);                    /* 0x00E06E88 */
            goto inhibit_end;
        }

        /* 0x00E06CE0..0x00E06CFE */
        aste->wire_count++;
        ML_$UNLOCK(AST_LOCK_ID);
        ML_$LOCK(PMAP_LOCK_ID);

        /* 0x00E06D00..0x00E06D1A: the first segment starts at the page
         * holding the aligned start; later ones at page 0 */
        if ((uint32_t)seg == start_seg) {
            first_page = (aligned_start & 0x7FFF) >> 10;
        } else {
            first_page = 0;
        }

        /* 0x00E06D1C..0x00E06D44: offset of that page relative to the
         * aligned start, and its segment map entry */
        rel_off = ((uint32_t)seg << 15) + (first_page << 10) - aligned_start;
        entry = (uint32_t *)((char *)PMAP_SEGMAP_ROW(aste->seg_index)
                             + (first_page << 2));

        /* 0x00E06D48..0x00E06D7A: last page to look at */
        if (map_size >= 0x20) {
            /* (length - seg_off) / 1KB, signed with the usual rounding
             * bias, clamped to 0x1F */
            pages_in_seg = (int32_t)(length - seg_off);
            if (pages_in_seg < 0) {
                pages_in_seg += 0x3FF;
            }
            pages_in_seg >>= 10;
            if (pages_in_seg > 0x1F) {
                pages_in_seg = 0x1F;
            }
            last_page = (uint32_t)pages_in_seg;
        } else {
            last_page = first_page + map_size - 1;
        }

        /* 0x00E06D7C..0x00E06D90: nothing to scan past the object's end
         * or when the range is empty (16-bit compare) */
        if (aligned_start > length) {
            goto unlock_pmap;
        }
        pages_left = (int16_t)((uint16_t)last_page - (uint16_t)first_page);
        if (pages_left < 0) {
            goto unlock_pmap;
        }

        do {
            /* 0x00E06D92..0x00E06D9C */
            present = 0;
            while ((int32_t)*entry < 0) {
                ast_$wait_for_page_transition();
            }

            /* 0x00E06D9E..0x00E06DA4: btst.l #0xe on the high word */
            if (*entry & SEGMAP_VALID) {
                /* 0x00E06DA6..0x00E06DEE */
                if (flags & 1) {
                    present = -1;
                } else {
                    mmape = &MMAPE_BASE[*entry & 0xFFFF];
                    /* bit 6 of the high word of disk_addr = bit 22 */
                    if ((mmape->disk_addr & 0x00400000u) == 0) {
                        present = -1;
                    } else if (mmape->flags2 & MMAPE_FLAG2_MODIFIED) {
                        /* bit 6 of the priority/flags2 word */
                        present = -1;
                    } else {
                        /* PFT low word bit 14 */
                        pft = PFT_FOR_PPN((uint16_t)(*entry & 0xFFFF));
                        if (*pft & PFT_FLAG_MODIFIED) {
                            present = -1;
                        }
                    }
                }
            } else {
                /* 0x00E06DF0..0x00E06E0A */
                if ((*entry & 0x3FFFFF) != 0) {
                    if (flags & 1) {
                        present = -1;
                    } else if ((*entry & 0x00400000u) == 0) {
                        /* btst.l #0x6 on the high word = bit 22 */
                        present = -1;
                    }
                }
            }

            /* 0x00E06E0C..0x00E06E64 */
            if (present < 0) {
                word_idx = (uint16_t)M$DIU$LLL(rel_off, seg_bytes);
                q = (uint32_t)M$OIS$LLL((long)rel_off, (long)seg_bytes) >> 10;
                bit = (uint16_t)M$DIU$LLL(q, seg_count);
                mask = ast_$seg_map_bit(bit);
                output[word_idx] |= mask;
            }

            /* 0x00E06E68..0x00E06E70 */
            entry++;
            rel_off += 0x400;
        } while (pages_left-- != 0);

unlock_pmap:
        /* 0x00E06E74..0x00E06E82 */
        ML_$UNLOCK(PMAP_LOCK_ID);
        aste->wire_count--;

inhibit_end:
        /* 0x00E06E96 */
        PROC1_$INHIBIT_END();

        /* 0x00E06E9C */
        if (is_remote < 0) {
            /*
             * 0x00E06EA2..0x00E06ECC: ask the object's home node; the
             * type flag is sne(flags bit 0).
             */
            REM_FILE_$GET_SEG_MAP(net_node, uid, start_offset, length,
                                  (flags & 1) ? 0xFF : 0x00, reply, status);
            /* 0x00E06ED0..0x00E06EEC: copy, or zero on failure (the
             * status is re-tested for every longword) */
            for (i = 0; i <= 0x7; i++) {
                if (*status != status_$ok) {
                    output[i] = 0;
                } else {
                    output[i] = reply[i];
                }
            }
        } else if (seg_count == 0) {
            /*
             * 0x00E06EF8..0x00E06F66: expand every set bit b of output[i]
             * into a run starting at b+1.  The run length divides by the
             * low word of seg_count (`divu.w (0x16,A6)`), which is zero on
             * this path, so the target raises a divide-by-zero trap the
             * first time a bit is found.  Reproduced as written.
             */
            for (i = 0; i <= 0x7; i++) {
                if (output[i] == 0) {
                    continue;
                }
                for (b = 0; b < 0x20; b++) {
                    if (b > 0x1F) {
                        break;
                    }
                    if ((output[i] & (1u << b)) == 0) {
                        continue;
                    }
                    run_start = (uint16_t)(b + 1);
                    q = (1u >= seg_count) ? 1u : 0u;
                    q = q / (uint16_t)(seg_count & 0xFFFF);
                    run_left = (int16_t)((uint16_t)q + b - 1 - run_start);
                    if (run_left < 0) {
                        continue;
                    }
                    bit = run_start;
                    do {
                        mask = ast_$seg_map_bit(bit);
                        output[i] |= mask;
                        bit++;
                    } while (run_left-- != 0);
                }
            }
        }

        /* 0x00E06F6A..0x00E06F7A: next segment; `subq.w / bcc` runs the
         * loop segs_left + 1 times */
        seg++;
        seg_off += 0x8000;
    } while (segs_left-- != 0);
    return;

unlock_and_return:
    /* 0x00E06CAA..0x00E06CB6 */
    ML_$UNLOCK(AST_LOCK_ID);
inhibit_end_and_return:
    /* 0x00E06CB8..0x00E06CBE */
    PROC1_$INHIBIT_END();
}
