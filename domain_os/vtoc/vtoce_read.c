/*
 * VTOCE_$READ - Read a VTOCE
 *
 * Original address: 0x00e394ec
 * Size: 490 bytes
 *
 * Reads a VTOCE given lookup request. Converts old format to new format
 * if necessary.
 */

#include "vtoc/vtoc_internal.h"

/* External variables */
extern int8_t NETWORK_$REALLY_DISKLESS;  /* 0xE24C4A */
extern int8_t NETLOG_$OK_TO_LOG;         /* 0xE248E0 */
extern uint32_t ROUTE_$PORT;             /* 0xE2E0A0 */
extern uint32_t NODE_$ME;                /* 0xE245A4 */
extern uint32_t VTOC_CACH_LOOKUPS;       /* 0xE78736 */

/* Internal function prototypes */
extern void NETLOG_$LOG_IT(int16_t type, uid_t *uid, int a, int b,
                           int16_t vol, int c, int d, int e);
extern void vtoc_$uid_cache_insert(uid_t *uid, int16_t vol_idx, uint32_t block_info);

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
    vol_idx_byte = *(uint8_t *)((uint8_t *)req + 0x1C);

    /* Check if diskless */
    if (NETWORK_$REALLY_DISKLESS < 0) {
        *status_ret = status_$VTOC_not_mounted;
        return;
    }

    /* Extract block and entry from request */
    block = req->block_hint >> 4;
    entry_idx = *(uint8_t *)((uint8_t *)req + 7) & 0x0F;

    ML_$LOCK(VTOC_LOCK_ID);

    vol_idx = vol_idx_byte;

    /* Check if mounted */
    if (vtoc_$data.mounted[vol_idx] >= 0) {
        *status_ret = status_$VTOC_not_mounted;
        goto done;
    }

    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        NETLOG_$LOG_IT(0x11, &req->uid, 0, 0, vol_idx, 0, 0, 0);
    }

    /* Get the VTOC block */
    buf = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID, block, 0, status_ret);

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

    /* Set write-protect flag in result based on cache flag */
    {
        char cache_flag = ((char *)&VTOC_CACH_LOOKUPS)[vol_idx + 3];
        uint8_t *result_byte = (uint8_t *)result + 3;
        *result_byte = (*result_byte & 0xFD) | ((cache_flag >> 7) * 2);
    }

    /* Fill in request fields */
    *(uint32_t *)req = 0;
    *(uint16_t *)((uint8_t *)req + 2) = *(uint16_t *)(OS_DISK_DATA + vol_idx * 2 - 2);
    ((uint32_t *)req)[4] = ROUTE_$PORT;
    ((uint32_t *)req)[5] = NODE_$ME;
    ((uint32_t *)req)[6] = 0;
    ((uint32_t *)req)[7] = 0;
    *(uint8_t *)((uint8_t *)req + 0x1D) |= 0x40;
    *(uint8_t *)((uint8_t *)req + 0x1C) = vol_idx_byte;
    *(uint8_t *)((uint8_t *)req + 0x1D) = (*(uint8_t *)((uint8_t *)req + 0x1D) & 0xF0) | 1;
    *(uint8_t *)((uint8_t *)req + 1) = (*(uint8_t *)((uint8_t *)req + 1) & 0xF0) | 1;

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
        uint32_t *result_uid = (uint32_t *)result + 1;
        uint32_t *req_uid = (uint32_t *)req + 2;

        if ((result_uid[0] != req_uid[0] || result_uid[1] != req_uid[1]) &&
            (req_uid[0] != UID_$NIL.high || req_uid[1] != UID_$NIL.low)) {
            *status_ret = 0x20008;  /* status_$uid_mismatch */
        }
    }

done:
    ML_$UNLOCK(VTOC_LOCK_ID);
}
