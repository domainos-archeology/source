/*
 * FM_$WRITE - Store one file-map entry (128 bytes) into a VTOC or FM block
 *
 * 0x00E3A45C - 0x00E3A5AC (338 bytes, A5 = OS_DISK_DATA at 0xE784D0).
 * Verified against the disassembly on 2026-09-19; the earlier emission
 * was faithful, this one cites the ranges and types the boolean.
 *
 * Arguments:
 *   (0x8,A6)  file_ref    -> fm_$file_ref_t (A3)
 *   (0xc,A6)  block_addr  longword (D5): block number << 4 | entry index
 *   (0x10,A6) level       word (D2)
 *   (0x12,A6) entry_in    -> 32 longwords (A4)
 *   (0x16,A6) write_now   BYTE in the high half of the word slot
 *                          (`move.b (0x16,A6),D0b`): negative = write the
 *                          block back at once (DBUF flags 0xb), else just
 *                          mark it dirty (0x9) - 0x00E3A486 - 0x00E3A492
 *   (0x18,A6) status      -> status_$t (A2)
 *
 * Same gating and block lookup as FM_$READ, except that a write-protected
 * volume (+0x26f + vol_idx) silently succeeds (0x00E3A4C8 - 0x00E3A4D4).
 * The entry is copied INTO the buffer at the same offsets FM_$READ uses,
 * then released with the chosen flags.
 */

#include "fm/fm_internal.h"

void FM_$WRITE(fm_$file_ref_t *file_ref, uint32_t block_addr, uint16_t level,
               fm_$entry_t *entry_in, int8_t write_now, status_$t *status)
{
    uint16_t vol_idx;               /* D4 / D3 */
    uint16_t set_flags;             /* (-0x18,A6) */
    uint32_t block_num;             /* D5 */
    uint16_t entry_idx;             /* D6 */
    uid_t local_uid;                /* (-0x8,A6) */
    uint32_t hint;                  /* (-0x10,A6) */
    uint16_t block_type;            /* (-0x1a,A6) */
    const uid_t *uid_src;           /* A1 */
    uint8_t *buffer;                /* A3 after the call */
    uint32_t *dst;
    int i;

    /* 0x00E3A46A - 0x00E3A4B0 */
    vol_idx = file_ref->vol_idx;
    set_flags = (write_now < 0) ? FM_BUF_WRITEBACK : FM_BUF_DIRTY;
    block_num = block_addr >> 4;
    entry_idx = (uint16_t)(block_addr & 0x0F);
    ML_$LOCK(FM_LOCK_ID);

    /* 0x00E3A4B2 - 0x00E3A4C4 */
    if (!VTOC_IS_MOUNTED(vol_idx)) {
        *status = status_$VTOC_not_mounted;
        goto done;
    }

    /* 0x00E3A4C8 - 0x00E3A4D4 */
    if (vtoc_$data.cach_wp_flag[vol_idx - 1] < 0) {
        *status = status_$ok;
        goto done;
    }

    /* 0x00E3A4D8 - 0x00E3A512 */
    if (level == 0) {
        hint = block_num;
        block_type = 0;
        uid_src = &VTOC_$UID;
    } else {
        int32_t l = (int32_t)level - 1;
        if (l < 0) {                    /* 0x00E3A4F2 bpl / addq #7 */
            l += 7;
        }
        hint = (uint32_t)((l >> 3) << 8) + 0x20;
        block_type = 1;
        uid_src = &file_ref->file_uid;
    }
    local_uid.high = uid_src->high;
    local_uid.low = uid_src->low;

    /* 0x00E3A516 - 0x00E3A53A */
    buffer = (uint8_t *)DBUF_$GET_BLOCK(vol_idx, (int32_t)block_num, &local_uid,
                                        hint, block_type, 0, status);
    if (*status != status_$ok) {
        goto done;
    }

    /* 0x00E3A53C - 0x00E3A580 */
    if (level != 0) {
        dst = (uint32_t *)(buffer + ((uint32_t)entry_idx << 7));
    } else if (VTOC_IS_NEW_FORMAT(vol_idx)) {
        dst = (uint32_t *)(buffer + entry_idx * FM_VTOCE_NEW_SIZE + FM_VTOCE_NEW_OFFSET);
    } else {
        dst = (uint32_t *)(buffer + entry_idx * FM_VTOCE_OLD_SIZE + FM_VTOCE_OLD_OFFSET);
    }
    for (i = 0; i < 32; i++) {
        dst[i] = entry_in->blocks[i];
    }

    /* 0x00E3A584 - 0x00E3A594 */
    DBUF_$SET_BUFF(buffer, set_flags, status);

done:
    /* 0x00E3A598 - 0x00E3A59E */
    ML_$UNLOCK(FM_LOCK_ID);
}
