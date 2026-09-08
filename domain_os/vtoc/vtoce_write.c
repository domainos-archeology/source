/*
 * VTOCE_$WRITE - Write a VTOCE
 *
 * Original address: 0x00e396d6
 * Size: 250 bytes
 *
 * Writes VTOCE data back to disk. Converts from new format to old format
 * if the volume uses old format.
 */

#include "vtoc/vtoc_internal.h"

/* Flags byte for old-to-new conversion at 0x00e38f7e (relative to PC) */
static char old_format_flags = 0;

void VTOCE_$WRITE(vtoc_$lookup_req_t *req, vtoce_$result_t *data, char flags,
                  status_$t *status_ret)
{
    uint8_t vol_idx_byte;
    uint16_t vol_idx;
    uint32_t block;
    uint32_t entry_idx;
    int16_t i;
    uint32_t *buf;
    uint32_t *src;
    uint32_t *dst;
    uint16_t dirty_flag;

    /* Get volume index from request (at offset 0x1C) */
    vol_idx_byte = req->vol_idx;

    /* Set dirty flag based on flags parameter */
    if (flags < 0) {
        dirty_flag = BAT_BUF_WRITEBACK;  /* 0xB - write back immediately */
    } else {
        dirty_flag = BAT_BUF_DIRTY;      /* 9 - mark dirty */
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

    /* Check if read-only (per-volume write-protect flag bit 7 set) */
    if (vtoc_$data.cach_wp_flag[vol_idx - 1] < 0) {
        /* Volume is read-only, silently succeed */
        *status_ret = status_$ok;
        goto done;
    }

    /* Get the VTOC block */
    buf = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID, block, 0, 0,
                                          status_ret);

    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Write VTOCE based on format */
    if (vtoc_$data.format[vol_idx] < 0) {
        /* New format - copy 0x90 bytes (0x24 longs) to entry offset */
        src = (uint32_t *)data;
        dst = (uint32_t *)((uint8_t *)buf + entry_idx * VTOCE_NEW_SIZE + 8);
        for (i = 0x23; i >= 0; i--) {
            *dst++ = *src++;
        }

        /* Clear bit 1 of status word at offset 0xA in the entry */
        {
            uint16_t *status_ptr = (uint16_t *)((uint8_t *)buf + entry_idx * VTOCE_NEW_SIZE + 10);
            *status_ptr &= 0xFFFD;
        }
    } else {
        /* Old format - convert from new format */
        VTOCE_$NEW_TO_OLD(data, &old_format_flags,
                          (uint8_t *)buf + entry_idx * VTOCE_OLD_SIZE + 4);
    }

    /* Release buffer with dirty flag */
    DBUF_$SET_BUFF(buf, dirty_flag, status_ret);

done:
    ML_$UNLOCK(VTOC_LOCK_ID);
}
