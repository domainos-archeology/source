/*
 * VTOCE_$WRITE - write a VTOCE back to its block
 *
 * Original address: 0x00E396D6 (SAU2 map: VTOC_, VTOCE_$WRITE at E396D6)
 * Size: 250 bytes (0x00E396D6 .. 0x00E397CF)
 *
 * Under ML lock 0x10: a mounted, writable volume gets the VTOC block named
 * by req->block_hint, the caller's 0x90-byte VTOCE is stored into entry
 * (block_hint & 0xF) - copied verbatim on a new-format volume, converted
 * with VTOCE_$NEW_TO_OLD on an old-format one - and the block is released
 * dirty (9) or written back at once (0xB) according to the flags byte.
 * A write-protected volume answers status_$ok without touching the disk.
 *
 * Frame (link.w A6,-0x10; D2-D4/A2-A5 saved; A5 = 0xE784D0 = &vtoc_$data):
 *   (0x8,A6)   req         pointer -> A0; byte +0x1c is the volume (D2w),
 *                          longword +4 the location
 *   (0xc,A6)   data        pointer -> D4
 *   (0x10,A6)  flags       byte (high half of its word slot) -> D0b;
 *                          negative selects write-back
 *   (0x12,A6)  status_ret  pointer -> A4
 *   (-0x8,A6)  block       block_hint >> 4
 *   (-0xc,A6)  dirty_op    0xB or 9, decided before the lock
 *   D3b        entry_idx   low nibble of req byte +7
 *   A2         buf
 *
 * Verified against the disassembly 2026-09-27; the body was already
 * faithful.  The NEW_TO_OLD flags cell (`pea (-0x81c,PC)` at 0x00E39798
 * -> 0x00E38F7E) is the SAME cell VTOC_$ALLOCATE passes at 0x00E38E5E,
 * so it now lives in vtoc_data.c instead of a private copy.
 */

#include "vtoc/vtoc_internal.h"

void VTOCE_$WRITE(vtoc_$lookup_req_t *req, vtoce_$result_t *data, char flags,
                  status_$t *status_ret)
{
    uint16_t vol_idx;               /* D2w */
    uint32_t block;                 /* (-0x8,A6) */
    uint16_t entry_idx;             /* D3 */
    uint16_t dirty_op;              /* (-0xc,A6) */
    void *buf;                      /* A2 */
    vtoce_$new_disk_t *nvt;
    const uint32_t *src;
    uint32_t *dst;
    int16_t i;

    /* 0x00E396F0 .. 0x00E396F6: `clr.w D2w` / `move.b (0x1c,A0),D2b` */
    vol_idx = req->vol_idx;

    /* 0x00E396FA .. 0x00E39706: `tst.b D0b` / `bpl` */
    if ((int8_t)flags < 0) {
        dirty_op = BAT_BUF_WRITEBACK;
    } else {
        dirty_op = BAT_BUF_DIRTY;
    }

    /* 0x00E3970C .. 0x00E3971A */
    block = req->block_hint >> 4;
    entry_idx = (uint16_t)(req->block_hint & 0x0F);

    /* 0x00E39718 .. 0x00E39728: ML_$LOCK(0x10) */
    ML_$LOCK(VTOC_LOCK_ID);

    /* 0x00E3972E `tst.b (0x277,A3)` / `bmi` */
    if (vtoc_$data.mounted[vol_idx] >= 0) {
        *status_ret = status_$VTOC_not_mounted;             /* 0x00E39734 */
        goto done;
    }

    /* 0x00E3973C `tst.b (0x26f,A3)` / `bpl`: write-protected -> ok, no I/O */
    if (vtoc_$data.cach_wp_flag[vol_idx - 1] < 0) {
        *status_ret = status_$ok;                           /* 0x00E39742 */
        goto done;
    }

    /* 0x00E39746 .. 0x00E39766: DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID
     * (0xE1739C), block, 0, 0, status_ret) */
    buf = DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID, block, 0, 0, status_ret);

    /* 0x00E39768 tst.l (A4) / bne.b 0x00e397ba */
    if (*status_ret != status_$ok) {
        goto done;
    }

    /* 0x00E3976C `tst.b (0x27f,A3)` / `bpl.b 0x00e3978e` */
    if (vtoc_$data.format[vol_idx] < 0) {
        /* 0x00E39772 .. 0x00E39782: 36 longwords into buf + 8 + idx*0x150 */
        nvt = &((vtoc_$vtoce_block_t *)buf)->entries[entry_idx];
        src = (const uint32_t *)data;
        dst = (uint32_t *)nvt;
        for (i = 0x23; i != -1; i--) {
            *dst++ = *src++;
        }
        /* 0x00E39786 `andi.w #-0x3,(0xa,A2,D0*0x1)`: clear bit 1 of the
         * entry's status word (+2) */
        nvt->hdr.status = (int16_t)(nvt->hdr.status & ~0x0002);
    } else {
        /* 0x00E3978E .. 0x00E397A2: VTOCE_$NEW_TO_OLD(data, &flags cell
         * 0x00E38F7E, buf + 4 + idx*0xCC) */
        VTOCE_$NEW_TO_OLD(data, &vtoc_$new_to_old_flags_00e38f7e,
                          &((vtoc_$old_block_t *)buf)->entries[entry_idx]);
    }

    /* 0x00E397A6 .. 0x00E397B6: DBUF_$SET_BUFF(buf, dirty_op, status_ret) */
    DBUF_$SET_BUFF(buf, dirty_op, status_ret);

done:
    /* 0x00E397BA .. 0x00E397C0: ML_$UNLOCK(0x10) */
    ML_$UNLOCK(VTOC_LOCK_ID);
}
