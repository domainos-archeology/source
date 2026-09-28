/*
 * VTOC_$LOOKUP - Look up a VTOCE by UID
 *
 * Original address: 0x00E38F80 (SAU2 map: VTOC_, VTOC_$LOOKUP at E38F80)
 * Size: 626 bytes (0x00E38F80 .. 0x00E391F1)
 *
 * Under ML lock 0x10: the UID cache is consulted first; on a miss the UID
 * is hashed to a (bucket, block) pair and the chain is walked - bucket
 * blocks of twenty (UID, location) slots on a new-format volume, VTOC
 * blocks of five VTOCEs on an old-format one - until the UID is found.
 * A hit fills req->block_hint with the VTOCE location and (new format
 * only) inserts it into the UID cache.  On success the rest of the
 * 0x20-byte request record is rewritten as an object location descriptor.
 *
 * Frame (link.w A6,-0x24; D2-D6/A2-A5 saved; A5 = 0xE784D0 = &vtoc_$data):
 *   (0x8,A6)   req          pointer -> A3
 *   (0xc,A6)   status_ret   pointer
 *   (-0x4,A6)  ignored      status cell of the in-loop release
 *   (-0x8,A6)  buf          the DBUF buffer
 *   (-0xc,A6)  block
 *   (-0xe,A6)  cache_flags  the word vtoc_$uid_cache_lookup fills in
 *   (-0x12,A6) bucket_idx   from vtoc_$hash_uid -> D4w
 *   (-0x24,A6) vol_base     A5 + vol_idx (the per-volume flag byte base)
 *   D2w        low byte written to req+0x1c on success: the cache flags
 *              word on a hit, the volume index on a miss
 *   D3b        found
 *
 * Re-emitted from the disassembly 2026-09-19.  The previous C kept
 * walking the chain after a failed DBUF_$GET_BLOCK (the image goes to the
 * common exit at 0x00E3918A) and wrote an uninitialised / wrong byte to
 * req+0x1c on the cache-miss paths (the image writes D2b, which is the
 * volume index there and the cache flags word on a hit).
 */

#include "vtoc/vtoc_internal.h"

void VTOC_$LOOKUP(vtoc_$lookup_req_t *req, status_$t *status_ret)
{
    uint16_t d2;                    /* D2w */
    uint16_t vol_idx;               /* D0w at 0x00E38FD2 */
    uint16_t cache_flags;           /* (-0xe,A6) */
    uint16_t bucket_idx;            /* D4w / (-0x12,A6) */
    uint32_t block;                 /* (-0xc,A6) */
    void *buf;                      /* (-0x8,A6) */
    boolean found;                  /* D3b */
    int8_t cache_result;            /* D0b */
    status_$t ignored;              /* (-0x4,A6) */
    vtoc_$bucket_entry_t *bkt;      /* A2 */
    vtoc_$old_block_t *oblk;        /* A1 */
    int16_t i;                      /* dbf counter */
    uint16_t k;                     /* D1w / D0w slot or entry index */

    /* 0x00E38F8E .. 0x00E38F9E: ML_$LOCK(0x10) */
    ML_$LOCK(VTOC_LOCK_ID);

    /* 0x00E38FA0 addq.l #0x1,(0x26c,A5): VTOC_CACH_LOOKUPS */
    vtoc_$data.cach_lookups++;

    /* 0x00E38FA4 .. 0x00E38FB8: vtoc_$uid_cache_lookup(&req->uid,
     * &cache_flags, &req->block_hint, 0) with a Pascal result slot */
    cache_result = (int8_t)vtoc_$uid_cache_lookup(&req->uid, &cache_flags,
                                                   &req->block_hint, 0);

    /* 0x00E38FBC move.w (-0xe,A6),D2w */
    d2 = cache_flags;

    /* 0x00E38FC0 tst.b D0b / bpl.b 0x00e38fd2 */
    if (cache_result < 0) {
        /* 0x00E38FC4 .. 0x00E38FCE: VTOC_CACH_HITS++, status ok, fill in */
        vtoc_$data.cach_hits++;
        *status_ret = status_$ok;
        goto fill_in;
    }

    /* 0x00E38FD2 .. 0x00E38FE0: vol_idx = req byte +0x1c, mounted? */
    vol_idx = req->vol_idx;
    if (vtoc_$data.mounted[vol_idx] >= 0) {
        *status_ret = status_$VTOC_not_mounted;     /* 0x00E38FE6 */
        goto fill_in;
    }

    /* 0x00E38FF0 .. 0x00E3900A: D2w = vol_idx; vtoc_$hash_uid(&req->uid,
     * vol_idx, &bucket_idx, &block, status_ret) */
    d2 = vol_idx;
    vtoc_$hash_uid(&req->uid, (short)vol_idx, &bucket_idx, &block, status_ret);

    /* 0x00E39012 tst.l (A0) / bne.w 0x00e391dc */
    if (*status_ret != status_$ok) {
        goto done;
    }

    /* 0x00E39020 clr.b D3b */
    found = false;

    for (;;) {
        /* 0x00E39026 .. 0x00E3902E: `tst.b (0x27f,A1)` / `bpl.w 0x00e390d8` */
        if (vtoc_$data.format[vol_idx] < 0) {
            /* 0x00E39032 .. 0x00E39056: DBUF_$GET_BLOCK(vol_idx, block,
             * &VTOC_BKT_$UID (0xE173AC), block, 0, 0, status_ret) */
            buf = DBUF_$GET_BLOCK(vol_idx, block, &VTOC_BKT_$UID, block, 0, 0, status_ret);

            /* 0x00E3905A tst.l (A0) / bne.w 0x00e3918a */
            if (*status_ret != status_$ok) {
                goto fill_in;
            }

            /* 0x00E39064 .. 0x00E39076: bkt = buf + bucket_idx * 0xF8 */
            bkt = (vtoc_$bucket_entry_t *)
                      ((uint8_t *)buf + (uint32_t)bucket_idx * VTOC_BUCKET_ENTRY_SIZE);

            /* 0x00E3907A .. 0x00E390C8: dbf #0x13 -> 20 slots; a slot with
             * a location and a matching UID is the answer */
            k = 0;
            for (i = 0x13; i != -1; i--) {
                if (bkt->slots[k].block_info != 0 &&
                    bkt->slots[k].uid.high == req->uid.high &&
                    bkt->slots[k].uid.low == req->uid.low) {
                    /* 0x00E390A4 .. 0x00E390BC: req->block_hint = the
                     * location; found; vtoc_$uid_cache_insert(&req->uid,
                     * vol_idx, req->block_hint) */
                    req->block_hint = bkt->slots[k].block_info;
                    found = true;
                    vtoc_$uid_cache_insert(&req->uid, (int16_t)vol_idx, req->block_hint);
                    break;
                }
                k++;
            }

            /* 0x00E390CC .. 0x00E390D0: follow the bucket chain */
            block = bkt->next_bucket;
            bucket_idx = bkt->next_bkt_idx;
        } else {
            /* 0x00E390D8 .. 0x00E390FC: DBUF_$GET_BLOCK(vol_idx, block,
             * &VTOC_$UID (0xE1739C), block, 0, 0, status_ret) */
            buf = DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID, block, 0, 0, status_ret);

            /* 0x00E39100 tst.l (A0) / bne.w 0x00e3918a */
            if (*status_ret != status_$ok) {
                goto fill_in;
            }

            oblk = (vtoc_$old_block_t *)buf;

            /* 0x00E3910E .. 0x00E39154: dbf #0x4 -> 5 entries from buf+0,
             * status word at +6 and UID at +8 (the 4-byte chain header) */
            k = 0;
            for (i = 4; i != -1; i--) {
                if (oblk->entries[k].hdr.status < 0 &&
                    oblk->entries[k].hdr.uid.high == req->uid.high &&
                    oblk->entries[k].hdr.uid.low == req->uid.low) {
                    /* 0x00E3912C .. 0x00E3914A: block_hint = (hint & 0xf)
                     * | (block << 4); low nibble of byte +7 := k; found */
                    req->block_hint = (req->block_hint & 0x0000000Fu) | (block << 4);
                    req->block_hint = (req->block_hint & 0xFFFFFFF0u) | ((uint8_t)k & 0x0F);
                    found = true;
                    break;
                }
                k++;
            }

            /* 0x00E39158 move.l (A1),(-0xc,A6): the chain word */
            block = oblk->next_block;
        }

        /* 0x00E3915C .. 0x00E39170: DBUF_$SET_BUFF(buf, 8, &ignored) */
        DBUF_$SET_BUFF(buf, BAT_BUF_CLEAN, &ignored);

        /* 0x00E39174 tst.b D3b / bmi.b 0x00e3918a */
        if (found < 0) {
            goto fill_in;
        }

        /* 0x00E39178 tst.l (-0xc,A6) / bne.w 0x00e39026 */
        if (block == 0) {
            break;
        }
    }

    /* 0x00E39180 .. 0x00E39184: ran off the chain */
    *status_ret = status_$VTOC_invalid_vtoce;

fill_in:
    /* 0x00E3918A .. 0x00E39190: only a successful lookup rewrites the record */
    if (*status_ret == status_$ok) {
        /* 0x00E39192 clr.l (A3) */
        req->flags = 0;

        /* 0x00E39194 .. 0x00E3919C: the word at req+2 (the low half of the
         * flags longword) := OS_DISK_DATA[vol_idx*2 - 2], vol_idx re-read
         * from req byte +0x1c */
        vol_idx = req->vol_idx;
        req->flags = (req->flags & 0xFFFF0000u) |
                     *(uint16_t *)(OS_DISK_DATA + vol_idx * 2 - 2);

        /* 0x00E391A2 .. 0x00E391B6 */
        req->port = ROUTE_$PORT;                    /* 0xE2E0A0 */
        req->node = NODE_$ME;                       /* 0xE245A4 */
        req->reserved_18 = 0;
        /* 0x00E391B6 clr.l (0x1c,A3) covers vol_idx, flags_1d, reserved_1e */
        req->vol_idx = 0;
        req->flags_1d = 0;
        req->reserved_1e = 0;

        /* 0x00E391BA .. 0x00E391CA */
        req->flags_1d |= 0x40;                      /* bset.b #6,(0x1d,A3) */
        req->vol_idx = (uint8_t)d2;                 /* move.b D2b,(0x1c,A3) */
        req->flags_1d = (uint8_t)((req->flags_1d & 0xF0) | 0x01);

        /* 0x00E391D0 / 0x00E391D6: byte +1 of the flags longword (bits
         * 16..23): low nibble := 1 */
        req->flags = (req->flags & ~0x000F0000u) | 0x00010000u;
    }

done:
    /* 0x00E391DC .. 0x00E391E2: ML_$UNLOCK(0x10) */
    ML_$UNLOCK(VTOC_LOCK_ID);
}
