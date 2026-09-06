/*
 * VTOC_$ALLOCATE - Allocate a new VTOCE
 *
 * Original address: 0x00e388ac
 * Size: 1746 bytes
 * Only caller: 0x00e5d746 (FILE_$PRIV_CREATE), which passes a 0x20-byte
 * location descriptor at (-0x58,A6), a VTOCE image at (-0x108,A6) whose
 * parent UID it has just filled in at +0x88, and a status cell.
 *
 * Allocates a VTOC entry for the object whose UID lives at new_vtoce+4 and
 * links it into the volume's hash chain.  Two on-disk organisations are
 * supported, selected by the per-volume format byte at vtoc_$data+0x27F:
 *
 *   old format (format >= 0, 0xE38CD4): VTOC blocks tagged with VTOC_$UID
 *     hold a 4-byte chain header and five 0xCC-byte VTOCEs.  The new VTOCE
 *     is converted down by VTOCE_$NEW_TO_OLD and written in place.
 *
 *   new format (format < 0, 0xE3893A): bucket blocks tagged with
 *     VTOC_BKT_$UID hold four 0xF8-byte buckets of twenty 12-byte
 *     (UID, location) slots.  The VTOCE itself lives in a separate block
 *     obtained from BAT_$ALLOC_VTOCE, three 0x150-byte entries per block.
 *
 * On every exit path - success, "not mounted", duplicate UID, or any I/O
 * failure - the 0x20-byte location descriptor is rewritten (0xE38F24).
 *
 * A5 = 0xE784D0 = OS_DISK_DATA = &vtoc_$data throughout.
 */

#include "vtoc/vtoc_internal.h"
#include "proc1/proc1.h"

/*
 * Constant cell at 0xE38F7E, passed by reference with `pea (0x11e,PC)` at
 * 0xE38E5E as VTOCE_$NEW_TO_OLD's second (var) argument.  The image holds
 * 0x00, i.e. "do not substitute the alternate parent UID".
 */
static char vtoc_$new_to_old_flags = 0;

/*
 * Process type that performs the (expensive) duplicate-UID chain walk before
 * allocating on a new-format volume; tested at 0xE38980 against
 * PROC1_$TYPE[PROC1_$CURRENT].
 */
#define VTOC_DUP_CHECK_PROC_TYPE    9

/* DBUF_$SET_BUFF operation codes used here (see dbuf/dbuf.h) */
#define VTOC_BUF_RELEASE        8       /* release, do not write */
#define VTOC_BUF_DIRTY          9       /* mark dirty, release */
#define VTOC_BUF_WRITEBACK      0x0B    /* write back now, release */

void VTOC_$ALLOCATE(vtoc_$lookup_req_t *loc, void *new_vtoce_p,
                    status_$t *status)
{
    vtoce_$hdr_t *new_vtoce = (vtoce_$hdr_t *)new_vtoce_p;
    vtoc_$vol_t *vol;                   /* A3 in the new-format branch, (-0x54,A6)
                                         * in the old-format branch */
    uint16_t vol_idx;                   /* D4w */
    uint32_t vtoce_loc;                 /* (-0xc,A6): block << 4 | entry */
    uint32_t block;                     /* (-0x24,A6) */
    uint32_t alloc_block;               /* (-0x28,A6) */
    uint16_t bucket_idx;                /* (-0x3a,A6) */
    int16_t free_slot;                  /* (-0x34,A6): free bucket slot */
    int16_t free_vtoce;                 /* (-0x36,A6) new fmt / D2w old fmt */
    uint16_t bkt_dirty;                 /* (-0x2e,A6) */
    uint16_t blk_dirty;                 /* (-0x30,A6) */
    int8_t new_blk_flag;                /* (-0x3c,A6): BAT_$ALLOC_VTOCE out */
    boolean new_bkt_block;              /* D2b */
    void *bkt_buf = NULL;               /* (-0x18,A6) */
    void *prev_buf = NULL;              /* (-0x1c,A6) */
    void *blk_buf = NULL;               /* A3 once it holds a buffer */
    void *cur_buf;                      /* (-0x8,A6): chain-walk buffer */
    status_$t ignored;                  /* (-0x10,A6): throw-away status cell */
    vtoc_$bucket_entry_t *bkt;
    vtoce_$new_disk_t *nvt;
    vtoce_$old_disk_t *ovt;
    uint32_t bkt_off;                   /* D5: bucket_idx * 0xF8 */
    uint32_t *src;
    uint32_t *dst;
    int32_t i;
    int16_t k;

    /* 0xE388BA-0xE388C4 */
    vol_idx = loc->vol_idx;
    vtoce_loc = loc->block_hint;

    /* 0xE388CE bset.b #0x7,(0x2,A1): mark the caller's VTOCE "in use" */
    new_vtoce->status |= VTOCE_STATUS_IN_USE;

    ML_$LOCK(VTOC_LOCK_ID);                             /* 0xE388DA */

    /* 0xE388E6 tst.b (0x277,A2) / bmi: mounted flags are 0xFF when mounted */
    if (vtoc_$data.mounted[vol_idx] >= 0) {
        *status = status_$VTOC_not_mounted;             /* 0xE388F0 */
        goto done;
    }

    prev_buf = NULL;                                    /* 0xE388FA */
    bkt_buf = NULL;                                     /* 0xE388FE */
    bkt_dirty = VTOC_BUF_RELEASE;                       /* 0xE38902 */

    /* 0xE38920: hash the UID at new_vtoce+4 to a (bucket, block) pair */
    vtoc_$hash_uid(&new_vtoce->uid, (short)vol_idx, &bucket_idx, &block, status);
    if (*status != status_$ok) {                        /* 0xE3892C */
        goto done;
    }

    /* 0xE38932 tst.b (0x27f,A2) / bpl: bit 7 clear selects the old format */
    if (vtoc_$data.format[vol_idx] >= 0) {
        goto old_format;                                /* 0xE38CD4 */
    }

    /*
     * ====================================================================
     * New (bucket) format
     * ====================================================================
     */
    vol = VTOC_VOL(vol_idx);                            /* 0xE3893A-0xE3893E */

    bkt_buf = DBUF_$GET_BLOCK(vol_idx, block, &VTOC_BKT_$UID,
                              block, 0, status);        /* 0xE38942 */
    if (*status != status_$ok) {                        /* 0xE3896C */
        goto done;
    }

    /* 0xE38972-0xE38986: only the file server walks the chain looking for
     * an existing VTOCE with this UID before allocating a new one. */
    if (PROC1_$TYPE[PROC1_$CURRENT] == VTOC_DUP_CHECK_PROC_TYPE) {
        uint16_t walk_idx = bucket_idx;                 /* D3w, 0xE3898E */
        uint32_t next_block;                            /* D2 */

        cur_buf = bkt_buf;                              /* 0xE3898A */

        for (;;) {                                      /* 0xE38992 */
            bkt = (vtoc_$bucket_entry_t *)
                      ((uint8_t *)cur_buf +
                       (uint32_t)walk_idx * VTOC_BUCKET_ENTRY_SIZE);
            next_block = bkt->next_bucket;              /* 0xE389A4 */
            walk_idx = bkt->next_bkt_idx;               /* 0xE389A6 */

            /* 0xE389AA-0xE389EE: dbf #0x13 -> 20 slots */
            for (k = 0; k <= 0x13; k++) {
                if (bkt->slots[k].block_info == 0) {    /* 0xE389B0 */
                    continue;
                }
                if (bkt->slots[k].uid.high == new_vtoce->uid.high &&
                    bkt->slots[k].uid.low == new_vtoce->uid.low) {
                    vtoce_loc = bkt->slots[k].block_info;    /* 0xE389D6 */
                    *status = status_$vtoc_duplicate_uid;    /* 0xE389E0 */
                    break;
                }
            }

            /* 0xE389F2: release the chained buffer, but never the one the
             * hash landed on - that stays locked for the allocation below. */
            if (bkt_buf != cur_buf) {
                DBUF_$SET_BUFF(cur_buf, VTOC_BUF_RELEASE, &ignored);
            }
            if (*status != status_$ok) {                /* 0xE38A18 */
                goto cleanup_bkt_buf;
            }
            if (next_block == 0) {                      /* 0xE38A1E */
                break;
            }
            cur_buf = DBUF_$GET_BLOCK(vol_idx, next_block, &VTOC_BKT_$UID,
                                      next_block, 0, status);   /* 0xE38A22 */
            if (*status != status_$ok) {                /* 0xE38A46 */
                goto cleanup_bkt_buf;
            }
        }
    }

    free_slot = -1;                                     /* 0xE38A50 */

rescan_bucket:                                          /* 0xE38A5C */
    bkt_off = (uint32_t)bucket_idx * VTOC_BUCKET_ENTRY_SIZE;
    bkt = (vtoc_$bucket_entry_t *)((uint8_t *)bkt_buf + bkt_off);

    /* 0xE38A70-0xE38A8C: dbf #0x13 -> 20 slots; first empty one wins */
    for (k = 0; k <= 0x13; k++) {
        if (bkt->slots[k].block_info == 0) {
            free_slot = k;                              /* 0xE38A80 */
            break;
        }
    }
    if (free_slot >= 0) {                               /* 0xE38A94 */
        goto have_free_slot;                            /* 0xE38B90 */
    }

    /* --- this bucket is full: move to (or create) the next one --- */
    new_bkt_block = false;                              /* 0xE38A98 clr.b D2b */

    if (bkt->next_bucket == 0) {                        /* 0xE38A9A */
        if (vol->cur_bkt_block == 0) {                  /* 0xE38A9E */
            /* 0xE38AA2: no partially filled bucket block, get a fresh one */
            BAT_$ALLOCATE((int16_t)vol_idx, block, 0x10000, &block, status);
            if (*status != status_$ok) {                /* 0xE38AC6 */
                goto cleanup_bkt_buf;
            }
            vol->cur_bkt_block = block;                 /* 0xE38ACC */
            vol->cur_bkt_idx = 0;                       /* 0xE38AD0 */
            new_bkt_block = true;                       /* 0xE38AD4 st D2b */
            bkt_dirty = VTOC_BUF_WRITEBACK;             /* 0xE38AD6 */
            vol->blocks_added++;                        /* 0xE38ADC */
        }

        /* 0xE38AE0-0xE38AF6: hand out the next bucket of that block */
        bucket_idx = vol->cur_bkt_idx;
        block = vol->cur_bkt_block;
        vol->cur_bkt_idx++;
        if (vol->cur_bkt_idx == VTOC_BKTS_PER_BLOCK) {
            vol->cur_bkt_block = 0;
        }

        bkt->next_bucket = block;                       /* 0xE38AF8 */
        bkt->next_bkt_idx = bucket_idx;                 /* 0xE38AFC */
        prev_buf = bkt_buf;                             /* 0xE38B02: keep the
                                                         * chain block until the
                                                         * new bucket is filled */
    } else {
        block = bkt->next_bucket;                       /* 0xE38B0A */
        bucket_idx = bkt->next_bkt_idx;                 /* 0xE38B0E */
        DBUF_$SET_BUFF(bkt_buf, VTOC_BUF_RELEASE, status);  /* 0xE38B14 */
    }

    /* 0xE38B2C-0xE38B58: flag 0x10 tells DBUF the block has no valid
     * contents yet, so it is not read from disk first. */
    bkt_buf = DBUF_$GET_BLOCK(vol_idx, block, &VTOC_BKT_$UID, block,
                              (new_bkt_block < 0) ? 0x10 : 0, status);
    if (*status != status_$ok) {                        /* 0xE38B64 */
        goto cleanup_prev_buf;
    }

    if (new_bkt_block < 0) {                            /* 0xE38B6A */
        /* 0xE38B70: dbf #0xfd -> 254 longwords */
        dst = (uint32_t *)bkt_buf;
        for (i = 0xFD; i >= 0; i--) {
            *dst++ = 0;
        }
        ((vtoc_$bkt_block_t *)bkt_buf)->magic = VTOC_BKT_BLOCK_MAGIC;   /* 0xE38B7E */
        ((vtoc_$bkt_block_t *)bkt_buf)->self_block = block;             /* 0xE38B86 */
    }
    goto rescan_bucket;                                 /* 0xE38B8C */

have_free_slot:                                         /* 0xE38B90 */
    alloc_block = vtoce_loc >> 4;                       /* 0xE38B90-0xE38B96 */
    blk_buf = BAT_$ALLOC_VTOCE((int16_t)vol_idx, alloc_block, &alloc_block,
                               status, &new_blk_flag);  /* 0xE38B9A */
    if (*status != status_$ok) {                        /* 0xE38BC0 */
        goto cleanup_bkt_buf;
    }

    /* 0xE38BC8-0xE38BE6: dbf #2 -> 3 VTOCEs per block; a non-negative
     * status word means the slot is free. */
    free_vtoce = -1;
    for (k = 0; k <= 2; k++) {
        nvt = (vtoce_$new_disk_t *)((uint8_t *)blk_buf +
                                    __builtin_offsetof(vtoc_$vtoce_block_t, entries) +
                                    (uint32_t)(uint16_t)k * VTOCE_NEW_SIZE);
        if (nvt->hdr.status >= 0) {
            free_vtoce = k;                             /* 0xE38BDA */
            break;
        }
    }
    if (free_vtoce == -1) {                             /* 0xE38BEA */
        *status = status_$VTOC_uid_mismatch;            /* 0xE38BF6 */
        goto cleanup_blk_buf;
    }

    nvt = (vtoce_$new_disk_t *)((uint8_t *)blk_buf +
                                __builtin_offsetof(vtoc_$vtoce_block_t, entries) +
                                (uint32_t)(uint16_t)free_vtoce * VTOCE_NEW_SIZE);

    /* 0xE38C04-0xE38C16: dbf #0x43 -> 68 longwords, clearing VTOCE bytes
     * 0x40..0x14F (everything past the region copied in below). */
    dst = (uint32_t *)((uint8_t *)nvt + 0x40);
    for (i = 0x43; i >= 0; i--) {
        *dst++ = 0;
    }

    new_vtoce->type_mode = 1;                           /* 0xE38C1E */

    /* 0xE38C26-0xE38C2A: dbf #0x23 -> 36 longwords = VTOCE bytes 0x00..0x8F */
    src = (uint32_t *)new_vtoce;
    dst = (uint32_t *)nvt;
    for (i = 0x23; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0xE38C2E: a brand-new VTOCE block must reach the disk immediately */
    blk_dirty = (new_blk_flag < 0) ? VTOC_BUF_WRITEBACK : VTOC_BUF_DIRTY;
    DBUF_$SET_BUFF(blk_buf, blk_dirty, status);         /* 0xE38C40 */
    if (*status != status_$ok) {                        /* 0xE38C5A */
        goto cleanup_bkt_buf;
    }

    /* 0xE38C60-0xE38C80: vtoce_loc := (alloc_block << 4) | (free_vtoce & 0xF) */
    vtoce_loc &= 0x0000000Fu;
    vtoce_loc |= alloc_block << 4;
    vtoce_loc &= ~(uint32_t)0x0000000Fu;
    vtoce_loc |= (uint32_t)((uint8_t)free_vtoce & 0x0F);

    /* 0xE38C84-0xE38CAE: publish the (UID, location) pair in the bucket.
     * D5 still holds the byte offset computed at 0xE38A5C. */
    bkt = (vtoc_$bucket_entry_t *)((uint8_t *)bkt_buf + bkt_off);
    bkt->slots[free_slot].uid.high = new_vtoce->uid.high;
    bkt->slots[free_slot].uid.low = new_vtoce->uid.low;
    bkt->slots[free_slot].block_info = vtoce_loc;

    DBUF_$SET_BUFF(bkt_buf, (uint16_t)(bkt_dirty | 1), status);     /* 0xE38CB0 */
    if (*status != status_$ok) {                        /* 0xE38CCA */
        goto cleanup_prev_buf;
    }
    goto success;                                       /* 0xE38CD0 */

    /*
     * ====================================================================
     * Old format
     * ====================================================================
     */
old_format:                                             /* 0xE38CD4 */
    vol = VTOC_VOL(vol_idx);                            /* 0xE38CD4-0xE38CDC */

    blk_buf = DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID,
                              block, 0, status);        /* 0xE38CE0 */
    if (*status != status_$ok) {                        /* 0xE38D06 */
        goto done;
    }

    blk_dirty = VTOC_BUF_DIRTY;                         /* 0xE38D0C */
    free_vtoce = -1;                                    /* 0xE38D12 moveq #-1,D2 */

scan_old_block:                                         /* 0xE38D1A */
    /* 0xE38D1C-0xE38D54: dbf #4 -> 5 entries per block.  The scan stops at
     * the first free entry; every in-use entry has its UID compared. */
    for (k = 0; k <= 4; k++) {
        ovt = &((vtoc_$old_block_t *)blk_buf)->entries[k];
        if (ovt->hdr.status >= 0) {
            free_vtoce = k;                             /* 0xE38D28 */
            break;
        }
        if (ovt->hdr.uid.high == new_vtoce->uid.high &&
            ovt->hdr.uid.low == new_vtoce->uid.low) {
            *status = status_$vtoc_duplicate_uid;       /* 0xE38D44 */
            goto cleanup_blk_buf;
        }
    }
    if (free_vtoce >= 0) {                              /* 0xE38D58 */
        goto old_have_entry;                            /* 0xE38E1E */
    }

    alloc_block = block;                                /* 0xE38D5E */
    block = ((vtoc_$old_block_t *)blk_buf)->next_block; /* 0xE38D64 */
    if (block != 0) {
        goto old_next_block;                            /* 0xE38DDA */
    }

    /* 0xE38D6A: extend the chain with a fresh VTOC block */
    BAT_$ALLOCATE((int16_t)vol_idx, alloc_block, 0x10000, &block, status);
    if (*status != status_$ok) {                        /* 0xE38D8E */
        goto cleanup_blk_buf;
    }
    ((vtoc_$old_block_t *)blk_buf)->next_block = block; /* 0xE38D94 */
    prev_buf = blk_buf;                                 /* 0xE38D98 */

    blk_buf = DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID,
                              block, 0x10, status);     /* 0xE38D9C */
    /* NOTE: the original checks no status here; it zeroes whatever A0 holds
     * (0xE38DBC dbf #0xff -> 256 longwords = the whole 1024-byte block). */
    dst = (uint32_t *)blk_buf;
    for (i = 0xFF; i >= 0; i--) {
        *dst++ = 0;
    }
    vol->blocks_added++;                                /* 0xE38DCC */
    blk_dirty = VTOC_BUF_WRITEBACK;                     /* 0xE38DD0 */
    goto scan_old_block;                                /* 0xE38DD6 */

old_next_block:                                         /* 0xE38DDA */
    DBUF_$SET_BUFF(blk_buf, VTOC_BUF_RELEASE, status);  /* 0xE38DDA */
    blk_buf = DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID,
                              block, 0, status);        /* 0xE38DF0 */
    if (*status != status_$ok) {                        /* 0xE38E14 */
        /* 0xE38E16 branches straight to the unlock: blk_buf is already
         * released and nothing else is held on this path. */
        goto done;
    }
    goto scan_old_block;                                /* 0xE38E1A */

old_have_entry:                                         /* 0xE38E1E */
    /* 0xE38E1E-0xE38E3C: vtoce_loc := (block << 4) | (free_vtoce & 0xF) */
    vtoce_loc &= 0x0000000Fu;
    vtoce_loc |= block << 4;
    vtoce_loc &= ~(uint32_t)0x0000000Fu;
    vtoce_loc |= (uint32_t)((uint8_t)free_vtoce & 0x0F);

    ovt = &((vtoc_$old_block_t *)blk_buf)->entries[free_vtoce];  /* 0xE38E46 */

    /* 0xE38E4A-0xE38E56: dbf #0x22 -> 35 longwords, clearing old-VTOCE
     * bytes 0x40..0xCB (the file map and trailing fields). */
    dst = (uint32_t *)((uint8_t *)ovt + 0x40);
    for (i = 0x22; i >= 0; i--) {
        *dst++ = 0;
    }

    /* 0xE38E5A-0xE38E66: the middle argument is the constant cell at
     * 0xE38F7E, passed by reference with pea (0x11e,PC). */
    VTOCE_$NEW_TO_OLD(new_vtoce, &vtoc_$new_to_old_flags, ovt);

    DBUF_$SET_BUFF(blk_buf, blk_dirty, status);         /* 0xE38E6E */
    if (*status != status_$ok) {                        /* 0xE38E88 */
        goto cleanup_bkt_buf;
    }
    /* fall through to success (0xE38E8C) */

success:                                                /* 0xE38E8C */
    if (prev_buf != NULL) {
        DBUF_$SET_BUFF(prev_buf, VTOC_BUF_DIRTY, &ignored);     /* 0xE38E92 */
    }
    /* 0xE38EAA: remember where the new VTOCE went */
    vtoc_$uid_cache_insert(&new_vtoce->uid, (int16_t)vol_idx, vtoce_loc);
    goto done;                                          /* 0xE38EBE */

cleanup_blk_buf:                                        /* 0xE38EC0 */
    if (blk_buf != NULL) {
        DBUF_$SET_BUFF(blk_buf, VTOC_BUF_RELEASE, &ignored);    /* 0xE38EC6 */
    }
    /* fall through */
cleanup_bkt_buf:                                        /* 0xE38EDC */
    if (bkt_buf != NULL) {
        DBUF_$SET_BUFF(bkt_buf, bkt_dirty, &ignored);           /* 0xE38EE2 */
    }
    /* fall through */
cleanup_prev_buf:                                       /* 0xE38EFA */
    if (prev_buf != NULL) {
        DBUF_$SET_BUFF(prev_buf, VTOC_BUF_DIRTY, &ignored);     /* 0xE38F00 */
    }
    /* fall through */

done:                                                   /* 0xE38F18 */
    ML_$UNLOCK(VTOC_LOCK_ID);

    /*
     * 0xE38F24-0xE38F72: rewrite the object location descriptor.  This runs
     * on every exit, including the failures above; loc->uid is not touched.
     */
    loc->flags = 0;                                     /* 0xE38F28 clr.l (A0) */
    /* 0xE38F2E: the word at loc+2 is the low half of the flags long */
    loc->flags = (loc->flags & 0xFFFF0000u) |
                 *(uint16_t *)(OS_DISK_DATA + vol_idx * 2 - 2);
    loc->block_hint = vtoce_loc;                        /* 0xE38F34 */
    loc->port = ROUTE_$PORT;                            /* 0xE38F3A */
    loc->node = NODE_$ME;                               /* 0xE38F42 */
    loc->reserved_18 = 0;                               /* 0xE38F4A */
    /* 0xE38F4E clr.l (0x1c,A0) covers vol_idx, flags_1d and reserved_1e */
    loc->vol_idx = 0;
    loc->flags_1d = 0;
    loc->reserved_1e = 0;
    loc->flags_1d |= 0x40;                              /* 0xE38F52 bset.b #6 */
    loc->vol_idx = (uint8_t)vol_idx;                    /* 0xE38F58 */
    loc->flags_1d = (uint8_t)((loc->flags_1d & 0xF0) | 0x01);   /* 0xE38F5C/62 */
    /* 0xE38F68/6E: byte 1 of the flags long = bits 16..23 */
    loc->flags = (loc->flags & ~0x000F0000u) | 0x00010000u;
}
