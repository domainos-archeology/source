/*
 * VTOC_$GET_UID - Get the UID stored at a VTOC (block, entry) position
 *
 * Original address: 0x00E391F2 (SAU2 map: VTOC_, VTOC_$GET_UID at E391F2)
 * Size: 508 bytes (0x00E391F2 .. 0x00E393ED)
 *
 * Syscall 0x1A.  The caller names a VTOC block by its index into the
 * volume's partition table and an entry index; the routine locates the
 * block through the partition table, follows the block chain (subtracting
 * the entries of every block passed over from entry_idx), and returns the
 * UID of the entry it lands on - unless that entry is empty, or is the
 * volume's own current VTOCE, in which case the status is "no UID"
 * (0x20004).  Running off the chain is "not found" (0x20005).
 *
 * Frame (link.w A6,-0x40; D2-D7/A2-A5 saved; A5 = 0xE784D0 = &vtoc_$data):
 *   (0x8,A6)   vol_idx_ptr    pointer to a word -> D4w
 *   (0xc,A6)   vtoc_idx_ptr   pointer to a word -> D0w / D2w
 *   (0x10,A6)  entry_idx_ptr  pointer to a WORD (`move.w (A2),D1w`) -> D3w;
 *                             A2 keeps this pointer until 0x00E392AA
 *   (0x14,A6)  uid_ret        pointer, always written (0x00E393CE)
 *   (0x18,A6)  status_ret     pointer, written last (0x00E393E0)
 *   (-0x8,A6)  local_uid      the UID handed to DBUF_$GET_BLOCK and
 *                             overwritten with the entry's UID
 *   (-0x10,A6) ignored        status cell of the final release
 *   (-0x14,A6) status         the status being built
 *   (-0x20,A6) bucket_idx     new format: vtoc_idx & 3, then next_bkt_idx
 *   (-0x3e,A6) vol_offset     vol_idx * 100
 *   D5w        partition count - 1 (9 new format, 7 old)
 *   D6         block
 *   A2         buf (zero from 0x00E392AA on)
 *
 * Re-emitted from the disassembly 2026-09-19.  The previous C read the
 * partition count from a fixed offset instead of the 6-byte {count, base}
 * pairs the loop steps through (`addq.l #0x6,A0` at 0x00E392A4), took the
 * third argument as a longword, released the in-loop buffer into the wrong
 * status cell, returned success for an old-format entry that IS the
 * current VTOCE (0x00E3938C .. 0x00E39396 make that "no UID"), and skipped
 * the not-mounted path's release of A2.  That last one is an original
 * quirk (bead source-cu7q): A2 still holds the caller's entry_idx pointer there.
 */

#include "vtoc/vtoc_internal.h"

void VTOC_$GET_UID(int16_t *vol_idx_ptr, uint16_t *vtoc_idx_ptr, uint16_t *entry_idx_ptr,
                   uid_t *uid_ret, status_$t *status_ret)
{
    int16_t vol_idx;                /* D4w */
    uint16_t vtoc_idx;              /* D2w: partition-relative block index */
    uint16_t entry_idx;             /* D3w */
    uint16_t bucket_idx;            /* (-0x20,A6) */
    int16_t part_limit;             /* D5w */
    int16_t i;                      /* D0w, the dbf counter */
    int16_t part;                   /* D1w */
    uint32_t block;                 /* D6 */
    uint32_t prev_block;            /* D0 in the old-format arm */
    void *buf;                      /* A2 */
    uid_t local_uid;                /* (-0x8,A6) */
    status_$t status;               /* (-0x14,A6) */
    status_$t ignored;              /* (-0x10,A6) */
    vtoc_$vol_t *vol;               /* A1 - 0x54 */
    vtoc_$bucket_entry_t *bkt;      /* A0 */
    vtoce_$old_disk_t *ovt;
    uint32_t block_info;            /* D5 */

    /* 0x00E39200 .. 0x00E3921A */
    vol_idx = *vol_idx_ptr;
    vtoc_idx = *vtoc_idx_ptr;
    entry_idx = *entry_idx_ptr;
    bucket_idx = vtoc_idx;
    buf = (void *)entry_idx_ptr;        /* A2 until 0x00E392AA - see the quirk */

    /* 0x00E3921C `tst.b (0x27f,A3)` / `bpl.b 0x00e39238` */
    if (vtoc_$data.format[vol_idx] < 0) {
        /* 0x00E39222 .. 0x00E39236: new format - ten partitions, the low two
         * bits of vtoc_idx select the bucket, the rest the bucket block */
        part_limit = 9;
        bucket_idx = vtoc_idx & 3;
        vtoc_idx >>= 2;
        local_uid = VTOC_BKT_$UID;                  /* 0xE173AC */
    } else {
        /* 0x00E39238 .. 0x00E39246: old format - eight partitions */
        part_limit = 7;
        local_uid = VTOC_$UID;                      /* 0xE1739C */
    }

    /* 0x00E3924A .. 0x00E39256: ML_$LOCK(0x10) */
    ML_$LOCK(VTOC_LOCK_ID);

    /* 0x00E39258 .. 0x00E39260 */
    vol = VTOC_VOL(vol_idx);

    /* 0x00E39264 `tst.b (0x277,A3)` / `bmi.b 0x00e39276` */
    if (vtoc_$data.mounted[vol_idx] >= 0) {
        status = status_$VTOC_not_mounted;          /* 0x00E3926A */
        goto cleanup;                               /* 0x00E39272 -> 0x00E393A6 */
    }

    /*
     * 0x00E39276 .. 0x00E392A6: walk the partition table.  `tst.w D5w` /
     * `bmi.b` skips the loop for a negative count (never, here); otherwise
     * `dbf D0w` runs part_limit + 1 times over the 6-byte {count, base}
     * pairs from vol-0x3c.  The first partition with vtoc_idx < count
     * yields block = base + vtoc_idx (zero-extended); otherwise vtoc_idx
     * is reduced by the count and the next pair is tried.
     */
    block = 0;
    if (part_limit >= 0) {
        part = 0;
        for (i = part_limit; i != -1; i--) {
            if (vtoc_idx < vol->parts[part].count) {            /* 0x00E39282 bcc */
                block = (uint32_t)vtoc_idx + vol->parts[part].base;   /* 0x00E39298 */
                break;
            }
            vtoc_idx = (uint16_t)(vtoc_idx - vol->parts[part].count); /* 0x00E3929E */
            part++;
        }
    }

    /* 0x00E392AA suba.l A2,A2 */
    buf = NULL;

    for (;;) {
        /* 0x00E392AC tst.l D6 / bne.b: end of chain */
        if (block == 0) {
            status = status_$VTOC_not_found;        /* 0x00E392B0 */
            goto cleanup;
        }

        /* 0x00E392BC .. 0x00E392D4: release the previous block INTO THE
         * STATUS CELL (-0x14,A6), not the throw-away one */
        if (buf != NULL) {
            DBUF_$SET_BUFF(buf, BAT_BUF_CLEAN, &status);
        }

        /* 0x00E392D8 .. 0x00E392F4: DBUF_$GET_BLOCK(vol_idx, block,
         * &local_uid, block, 0, 0, &status) */
        buf = DBUF_$GET_BLOCK((uint16_t)vol_idx, block, &local_uid, block, 0, 0, &status);

        /* 0x00E392F6 tst.l (-0x14,A6) / bne.w 0x00e393a6 */
        if (status != status_$ok) {
            goto cleanup;
        }

        /* 0x00E392FE `tst.b (0x27f,A3)` / `bpl.b 0x00e39358` */
        if (vtoc_$data.format[vol_idx] < 0) {
            /* 0x00E39304 .. 0x00E3931A: bucket = buf + bucket_idx * 0xF8,
             * block = its next_bucket, bucket_idx = its next_bkt_idx */
            bkt = (vtoc_$bucket_entry_t *)
                      ((uint8_t *)buf + (uint32_t)bucket_idx * VTOC_BUCKET_ENTRY_SIZE);
            block = bkt->next_bucket;
            bucket_idx = bkt->next_bkt_idx;

            /* 0x00E39320 `cmpi.w #0x14,D3w` / `bcc.b 0x00e39350` */
            if (entry_idx >= VTOCE_BUCKET_SLOTS) {
                entry_idx = (uint16_t)(entry_idx - VTOCE_BUCKET_SLOTS);   /* 0x00E39350 */
                continue;
            }

            /* 0x00E39326 .. 0x00E3933C: local_uid = slot uid */
            local_uid.high = bkt->slots[entry_idx].uid.high;
            local_uid.low = bkt->slots[entry_idx].uid.low;

            /* 0x00E39340 `move.l (0x10,A0,D0*0x1),D5` / `beq.b 0x00e39396` */
            block_info = bkt->slots[entry_idx].block_info;
            if (block_info == 0) {
                status = status_$no_UID;            /* 0x00E39396 */
                goto cleanup;
            }

            /* 0x00E39346 .. 0x00E39394: the volume's current VTOCE is not
             * reported; any other location is success (status still 0) */
            if (block_info != vol->current_vtoce) {
                goto cleanup;
            }
            status = status_$no_UID;                /* 0x00E39396 */
            goto cleanup;
        }

        /* 0x00E39358 .. 0x00E3935A: old format - remember this block,
         * block = the chain word at +0 */
        prev_block = block;
        block = ((vtoc_$old_block_t *)buf)->next_block;

        /* 0x00E3935C `cmpi.w #0x5,D3w` / `bcc.b 0x00e393a0` */
        if (entry_idx >= VTOCE_OLD_ENTRIES_PER_BLOCK) {
            entry_idx = (uint16_t)(entry_idx - VTOCE_OLD_ENTRIES_PER_BLOCK);   /* 0x00E393A0 */
            continue;
        }

        /* 0x00E39362 .. 0x00E39374: entry at buf + entry_idx*0xCC (the
         * 4-byte chain header puts its UID at +8 and its status at +6) */
        ovt = &((vtoc_$old_block_t *)buf)->entries[entry_idx];
        local_uid.high = ovt->hdr.uid.high;
        local_uid.low = ovt->hdr.uid.low;

        /* 0x00E39378 `tst.w (0x6,A0)` / `bpl.b 0x00e39396`: free entry */
        if (ovt->hdr.status >= 0) {
            status = status_$no_UID;                /* 0x00E39396 */
            goto cleanup;
        }

        /* 0x00E3937E .. 0x00E39394: (current_vtoce >> 4) == prev_block and
         * (current_vtoce & 0xf) == entry_idx means this IS the current
         * VTOCE -> no UID; anything else is success */
        if ((vol->current_vtoce >> 4) != prev_block) {
            goto cleanup;
        }
        if ((uint16_t)(vol->current_vtoce & 0x0F) != entry_idx) {
            goto cleanup;
        }
        status = status_$no_UID;                    /* 0x00E39396 */
        goto cleanup;
    }

cleanup:
    /*
     * 0x00E393A6 `cmpa.w #0x0,A2` / `beq.b`: release the buffer into the
     * throw-away cell.  On the not-mounted path A2 was never cleared and
     * still holds the caller's entry_idx pointer, which is therefore
     * handed to DBUF_$SET_BUFF - reproduced as found (original bug, bead source-cu7q).
     */
    if (buf != NULL) {
        DBUF_$SET_BUFF(buf, BAT_BUF_CLEAN, &ignored);
    }

    /* 0x00E393C2 .. 0x00E393C8: ML_$UNLOCK(0x10) */
    ML_$UNLOCK(VTOC_LOCK_ID);

    /* 0x00E393CE .. 0x00E393E0 */
    uid_ret->high = local_uid.high;
    uid_ret->low = local_uid.low;
    *status_ret = status;
}
