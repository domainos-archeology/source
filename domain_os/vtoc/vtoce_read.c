/*
 * VTOCE_$READ - Read a VTOCE
 *
 * Original address: 0x00e394ec
 * Size: 490 bytes
 *
 * Reads a VTOCE given lookup request. Converts old format to new format
 * if necessary.
 *
 * Frame (link.w A6,-0x14; D2-D6/A2-A5 saved; A5 = 0xE784D0 = &vtoc_$data):
 *   (0x8,A6)   req         pointer -> A3
 *   (0xc,A6)   result      pointer -> D5
 *   (0x10,A6)  status_ret  pointer -> A4
 *   (-0x4,A6)  cache_info  the location handed to vtoc_$uid_cache_insert:
 *                          its low nibble starts as stack garbage
 *                          (`andi.l #0xf,(-0x4,A6)` at 0x00E39630) and is
 *                          overwritten with the entry number before every
 *                          insert (0x00E39654 .. 0x00E3965C)
 *   (-0xc,A6)  block       req->block_hint >> 4
 *   D2w        vol_idx     req byte +0x1c, zero-extended
 *   D3b        entry_idx   low nibble of req byte +7
 *   D6         buf
 *
 * NETLOG_$LOG_IT is called with six pushes (0x00E3955A .. 0x00E39566:
 * `clr.l`, `clr.w`, vol_idx, `clr.l`, &uid, 0x11) which fill the eight
 * word/long slots of its prototype as (0x11, &uid, 0, 0, vol_idx, 0, 0, 0).
 *
 * Re-checked against the disassembly 2026-09-19 (0x00E394EC .. 0x00E396D5):
 * the body was already faithful.
 */

#include "vtoc/vtoc_internal.h"

void VTOCE_$READ(vtoc_$lookup_req_t *req, vtoce_$result_t *result, status_$t *status_ret)
{
    uint8_t vol_idx_byte;
    uint16_t vol_idx;
    uint32_t block;
    uint32_t entry_idx;
    int16_t i;
    uint32_t *buf;
    uint32_t *src;
    uint32_t *dst;
    uint32_t block_info;
    uint8_t entry_num;

    /* Get volume index from request (at offset 0x1C) */
    vol_idx_byte = req->vol_idx;

    /* Check if diskless */
    if (NETWORK_$REALLY_DISKLESS < 0) {
        *status_ret = status_$VTOC_not_mounted;
        return;
    }

    /* Extract block and entry from request */
    block = req->block_hint >> 4;
    entry_idx = req->block_hint & 0x0F;     /* low nibble (byte +7 on m68k) */

    ML_$LOCK(VTOC_LOCK_ID);

    vol_idx = vol_idx_byte;

    /* Check if mounted */
    if (vtoc_$data.mounted[vol_idx] >= 0) {
        *status_ret = status_$VTOC_not_mounted;
        goto done;
    }

    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        NETLOG_$LOG_IT(0x11, (uint32_t *)&req->uid, 0, 0, vol_idx, 0, 0, 0);
    }

    /* Get the VTOC block */
    buf = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID, block, 0, 0,
                                          status_ret);

    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Read VTOCE based on format */
    if (vtoc_$data.format[vol_idx] < 0) {
        /* New format - copy 0x90 bytes (0x24 longs) from entry offset */
        src = (uint32_t *)((uint8_t *)buf + entry_idx * VTOCE_NEW_SIZE + 8);
        dst = (uint32_t *)result;
        for (i = 0x23; i >= 0; i--) {
            *dst++ = *src++;
        }
    } else {
        /* Old format - convert to new format */
        VTOCE_$OLD_TO_NEW((uint8_t *)buf + entry_idx * VTOCE_OLD_SIZE + 4, result);
    }

    /* Set write-protect flag in result based on the per-volume flag */
    {
        int8_t wp_flag = vtoc_$data.cach_wp_flag[vol_idx - 1];
        uint8_t *result_byte = &result->data[3];
        *result_byte = (*result_byte & 0xFD) | ((wp_flag < 0) ? 2 : 0);
    }

    /* Fill in request fields */
    req->flags = 0;
    /* bytes 2-3 of the flags word (low 16 bits on m68k) */
    req->flags = (req->flags & 0xFFFF0000u) |
                 *(uint16_t *)(OS_DISK_DATA + vol_idx * 2 - 2);
    req->port = ROUTE_$PORT;
    req->node = NODE_$ME;
    req->reserved_18 = 0;
    req->vol_idx = 0;
    req->flags_1d = 0;
    req->reserved_1e = 0;
    req->flags_1d |= 0x40;
    req->vol_idx = vol_idx_byte;
    req->flags_1d = (req->flags_1d & 0xF0) | 1;
    /* Byte +1 of the flags word (bits 16..23 on m68k): low nibble := 1 */
    req->flags = (req->flags & ~0x000F0000u) | 0x00010000u;

    /* For new format, update UID cache for all valid entries in block */
    if (vtoc_$data.format[vol_idx] < 0) {
        block_info = (req->block_hint & 0xF) | ((req->block_hint >> 4) << 4);
        entry_num = 0;

        for (i = 2; i >= 0; i--) {
            uint8_t *entry_ptr = (uint8_t *)buf + entry_num * VTOCE_NEW_SIZE;

            /* Check if entry is valid (status word < 0) */
            if (*(int16_t *)(entry_ptr + 10) < 0) {
                uint32_t cache_info = (block_info & 0xFFFFFFF0) | entry_num;
                vtoc_$uid_cache_insert((uid_t *)(entry_ptr + 0x0C), vol_idx, cache_info);
            }

            entry_num++;
        }
    }

    /* Release buffer */
    DBUF_$SET_BUFF(buf, BAT_BUF_CLEAN, status_ret);

    /* Verify UID matches (unless request UID is nil) */
    {
        uint32_t *result_uid = (uint32_t *)&result->data[4];

        if ((result_uid[0] != req->uid.high || result_uid[1] != req->uid.low) &&
            (req->uid.high != UID_$NIL.high || req->uid.low != UID_$NIL.low)) {
            *status_ret = 0x20008;  /* status_$uid_mismatch */
        }
    }

done:
    ML_$UNLOCK(VTOC_LOCK_ID);
}
