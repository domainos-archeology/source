/*
 * FM_$READ - Read one file-map entry (128 bytes) out of a VTOC or FM block
 *
 * 0x00E3A314 - 0x00E3A45A (328 bytes, A5 = OS_DISK_DATA at 0xE784D0).
 * Verified against the disassembly on 2026-09-19; the earlier emission
 * was faithful, this one cites the ranges.
 *
 * Arguments:
 *   (0x8,A6)  file_ref    -> fm_$file_ref_t (A3): vol_idx byte at +0x1c,
 *                          file uid at +0x08
 *   (0xc,A6)  block_addr  longword (D4): block number << 4 | entry index
 *   (0x10,A6) level       word (D5): 0 = the entry lives in a VTOCE,
 *                          else in a file-map block
 *   (0x12,A6) entry_out   -> 32 longwords
 *   (0x16,A6) status      -> status_$t (A2)
 *
 * Under ML lock 0x10: the volume must be mounted (OS_DISK_DATA + 0x277 +
 * vol_idx negative, else status_$VTOC_not_mounted); a write-protected
 * volume (+0x26f + vol_idx) returns 32 zero longwords for block 1 entry
 * 15 (0x00E3A366 - 0x00E3A38A).  DBUF_$GET_BLOCK is called with the VTOC
 * UID / hint = block / type 0 for level 0, or the file's UID / hint =
 * ((level - 1) >> 3) * 0x100 + 0x20 / type 1 otherwise, and flags 0.  The
 * entry is at idx * 0x80 in an FM block, or in the VTOCE at idx * 0x150 +
 * 0xd8 (new format, +0x27f + vol_idx negative) or idx * 0xcc + 0x44 (old).
 * The buffer is released with DBUF_$SET_BUFF(…, 8).
 */

#include "fm/fm_internal.h"

/* 0x00E3A346: `move.w #0x10,-(SP)` to ML_$LOCK */

void FM_$READ(fm_$file_ref_t *file_ref, uint32_t block_addr, uint16_t level,
              fm_$entry_t *entry_out, status_$t *status)
{
    uint16_t vol_idx;               /* D2: byte, zero-extended */
    uint32_t block_num;             /* D7 */
    uint16_t entry_idx;             /* D6 = D3 */
    uid_t local_uid;                /* (-0x8,A6) */
    uint32_t hint;                  /* (-0x14,A6) */
    uint16_t block_type;            /* (-0x1c,A6) */
    const uid_t *uid_src;           /* A0 */
    uint8_t *buffer;                /* A3 after the call */
    const uint32_t *src;            /* A0 */
    int i;

    /* 0x00E3A322 - 0x00E3A344 */
    vol_idx = file_ref->vol_idx;
    block_num = block_addr >> 4;
    entry_idx = (uint16_t)(block_addr & 0x0F);
    ML_$LOCK(FM_LOCK_ID);

    /* 0x00E3A352 - 0x00E3A362 */
    if (!VTOC_IS_MOUNTED(vol_idx)) {
        *status = status_$VTOC_not_mounted;
        goto done;
    }

    /* 0x00E3A366 - 0x00E3A38A */
    if (vtoc_$data.cach_wp_flag[vol_idx - 1] < 0 && block_num == 1 && entry_idx == 0x0F) {
        *status = status_$ok;
        for (i = 0; i < 32; i++) {
            entry_out->blocks[i] = 0;
        }
        goto done;
    }

    /* 0x00E3A38E - 0x00E3A3C8 */
    if (level == 0) {
        hint = block_num;
        block_type = 0;
        uid_src = &VTOC_$UID;
    } else {
        int32_t l = (int32_t)level - 1;
        if (l < 0) {                    /* 0x00E3A3A8 bpl / addq #7: never for a word level */
            l += 7;
        }
        hint = (uint32_t)((l >> 3) << 8) + 0x20;
        block_type = 1;
        uid_src = &file_ref->file_uid;
    }
    local_uid.high = uid_src->high;
    local_uid.low = uid_src->low;

    /* 0x00E3A3CC - 0x00E3A3F0: flags word 0 (`clr.w`) */
    buffer = (uint8_t *)DBUF_$GET_BLOCK(vol_idx, (int32_t)block_num, &local_uid,
                                        hint, block_type, 0, status);
    if (*status != status_$ok) {
        goto done;
    }

    /* 0x00E3A3F2 - 0x00E3A422 */
    if (level != 0) {
        src = (const uint32_t *)(buffer + ((uint32_t)entry_idx << 7));
    } else if (VTOC_IS_NEW_FORMAT(vol_idx)) {
        src = (const uint32_t *)(buffer + entry_idx * FM_VTOCE_NEW_SIZE + FM_VTOCE_NEW_OFFSET);
    } else {
        src = (const uint32_t *)(buffer + entry_idx * FM_VTOCE_OLD_SIZE + FM_VTOCE_OLD_OFFSET);
    }

    /* 0x00E3A426 - 0x00E3A42E: moveq #0x1f / dbf = 32 longwords */
    for (i = 0; i < 32; i++) {
        entry_out->blocks[i] = src[i];
    }

    /* 0x00E3A432 - 0x00E3A442 */
    DBUF_$SET_BUFF(buffer, FM_BUF_RELEASE, status);

done:
    /* 0x00E3A446 - 0x00E3A44C */
    ML_$UNLOCK(FM_LOCK_ID);
}
