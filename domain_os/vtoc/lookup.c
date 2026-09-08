/*
 * VTOC_$LOOKUP - Look up a VTOCE by UID
 *
 * Original address: 0x00e38f80
 * Size: 626 bytes
 *
 * Searches for a VTOCE with the given UID. Uses hash-based lookup
 * on new format volumes or linear search on old format volumes.
 */

#include "vtoc/vtoc_internal.h"

void VTOC_$LOOKUP(vtoc_$lookup_req_t *req, status_$t *status_ret)
{
    uint16_t vol_idx;
    int16_t i;
    uint8_t found;
    uint8_t entry_idx;
    uint16_t bucket_idx;
    uint32_t block;
    uint32_t *buf;
    uint32_t *entry_ptr;
    uint32_t *bucket_entry;
    uint16_t flags;
    status_$t local_status;
    uint8_t cache_result;

    ML_$LOCK(VTOC_LOCK_ID);

    /* Increment lookup counter (VTOC_CACH_LOOKUPS, 0xE7873C) */
    vtoc_$data.cach_lookups++;

    /* First check UID cache */
    cache_result = vtoc_$uid_cache_lookup(&req->uid, &flags, &req->block_hint, 0);

    if (cache_result < 0) {
        /* Cache hit (VTOC_CACH_HITS, 0xE78738) */
        vtoc_$data.cach_hits++;
        *status_ret = status_$ok;
        entry_idx = (uint8_t)(flags & 0xFF);
    } else {
        /* Cache miss - do disk lookup */
        vol_idx = req->vol_idx;  /* vol_idx at offset 0x1C */

        if (vtoc_$data.mounted[vol_idx] < 0) {
            /* Volume is mounted, hash the UID to find starting block */
            vtoc_$hash_uid(&req->uid, vol_idx, &bucket_idx, &block, status_ret);

            if (*status_ret != status_$ok) {
                goto done;
            }

            found = 0;

            do {
                if (vtoc_$data.format[vol_idx] < 0) {
                    /* New format - bucket lookup */
                    buf = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, block, &VTOC_BKT_$UID,
                                                      block, 0, 0, status_ret);
                    if (*status_ret != status_$ok) {
                        goto check_found;
                    }

                    /* Calculate bucket entry pointer
                     * Each bucket entry is 0xF8 (248) bytes
                     * Calculation: bucket_idx * 0xF8 = bucket_idx * (0x100 - 8)
                     *            = bucket_idx * (-8 + 256) = bucket_idx * 0x3E * 4
                     * This is: (bucket_idx << 3) - bucket_idx gives us bucket_idx * 7
                     * Then: (bucket_idx * -8 + bucket_idx * 256) = bucket_idx * 248 / 4 = 62
                     */
                    bucket_entry = buf + (uint32_t)bucket_idx * 0x3E;

                    /* Search through 20 slots in this bucket entry */
                    entry_ptr = bucket_entry;
                    for (i = 0x13; i >= 0; i--) {
                        if (entry_ptr[4] != 0) {  /* Check block_info != 0 */
                            /* Compare UID at offset 8 in entry */
                            if (entry_ptr[2] == req->uid.high &&
                                entry_ptr[3] == req->uid.low) {
                                /* Found! Get block_info */
                                req->block_hint = bucket_entry[(uint32_t)(0x13 - i) * 3 + 4];
                                found = 0xFF;

                                /* Insert into cache */
                                vtoc_$uid_cache_insert(&req->uid, vol_idx, req->block_hint);
                                break;
                            }
                        }
                        entry_ptr += 3;  /* Each slot is 12 bytes = 3 longs */
                    }

                    /* Get next bucket block and index */
                    block = *bucket_entry;
                    bucket_idx = *(uint16_t *)(bucket_entry + 1);

                } else {
                    /* Old format - linear search through VTOC blocks */
                    buf = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, block, &VTOC_$UID,
                                                      block, 0, 0, status_ret);
                    if (*status_ret != status_$ok) {
                        goto check_found;
                    }

                    /* Search through 5 entries in this VTOC block
                     * Each entry is 0xCC (204) bytes = 0x33 longs
                     */
                    entry_ptr = buf;
                    entry_idx = 0;
                    for (i = 4; i >= 0; i--) {
                        /* Check if entry is valid (status word < 0) */
                        if (*(int16_t *)((uint8_t *)entry_ptr + 6) < 0) {
                            /* Compare UID at offset 8 */
                            if (entry_ptr[2] == req->uid.high &&
                                entry_ptr[3] == req->uid.low) {
                                /* Found! Build block_hint */
                                req->block_hint = (req->block_hint & 0xF) | (block << 4);
                                /* Low nibble of block_hint (byte +7 on m68k) := entry index */
                                req->block_hint = (req->block_hint & 0xFFFFFFF0u) | entry_idx;
                                found = 0xFF;
                                break;
                            }
                        }
                        entry_idx++;
                        entry_ptr += 0x33;  /* Each entry is 0xCC bytes = 0x33 longs */
                    }

                    /* Get next block in chain */
                    block = *buf;
                }

                /* Release buffer */
                DBUF_$SET_BUFF(buf, BAT_BUF_CLEAN, &local_status);

check_found:
                if (found) {
                    goto check_status;
                }
            } while (block != 0);

            /* Not found */
            *status_ret = status_$VTOC_invalid_vtoce;  /* 0x20006 */

        } else {
            /* Volume not mounted */
            *status_ret = status_$VTOC_not_mounted;
        }
    }

check_status:
    /* On success, fill in additional request fields */
    if (*status_ret == status_$ok) {
        /* Clear first long */
        req->flags = 0;

        /* Set word at offset 2 (low 16 bits of the flags word on m68k) from
         * per-volume data */
        vol_idx = req->vol_idx;
        req->flags = (req->flags & 0xFFFF0000u) |
                     *(uint16_t *)(OS_DISK_DATA + vol_idx * 2 - 2);

        /* Set network info */
        req->port = ROUTE_$PORT;
        req->node = NODE_$ME;
        req->reserved_18 = 0;
        req->vol_idx = 0;
        req->flags_1d = 0;
        req->reserved_1e = 0;

        /* Set flags at offset 0x1D */
        req->flags_1d |= 0x40;
        req->vol_idx = entry_idx;
        req->flags_1d = (req->flags_1d & 0xF0) | 1;
        /* Byte +1 of the flags word (bits 16..23 on m68k): low nibble := 1 */
        req->flags = (req->flags & ~0x000F0000u) | 0x00010000u;
    }

done:
    ML_$UNLOCK(VTOC_LOCK_ID);
}
