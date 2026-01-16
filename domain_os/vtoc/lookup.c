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

/* External variables */
extern uint32_t VTOC_CACH_LOOKUPS;   /* Cache lookup counter at 0xE78736 */
extern uint32_t VTOC_CACH_HITS;      /* Cache hit counter at 0xE78732 */
extern uint32_t ROUTE_$PORT;         /* Network route port at 0xE2E0A0 */
extern uint32_t NODE_$ME;            /* This node's ID at 0xE245A4 */

/* Internal function prototypes */
extern uint8_t vtoc_$uid_cache_lookup(uid_t *uid, uint16_t *flags, uint32_t *block_info, char update);
extern void vtoc_$hash_uid(uid_t *uid, int16_t vol_idx, uint16_t *bucket_idx,
                           uint32_t *block, status_$t *status);
extern void vtoc_$uid_cache_insert(uid_t *uid, int16_t vol_idx, uint32_t block_info);

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

    /* Increment lookup counter */
    VTOC_CACH_LOOKUPS++;

    /* First check UID cache */
    cache_result = vtoc_$uid_cache_lookup(&req->uid, &flags, &req->block_hint, 0);

    if (cache_result < 0) {
        /* Cache hit */
        VTOC_CACH_HITS++;
        *status_ret = status_$ok;
        entry_idx = (uint8_t)(flags & 0xFF);
    } else {
        /* Cache miss - do disk lookup */
        vol_idx = *(uint8_t *)((uint8_t *)req + 0x1C);  /* vol_idx at offset 0x1C */

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
                                                      block, 0, status_ret);
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
                                                      block, 0, status_ret);
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
                                *(uint8_t *)((uint8_t *)req + 7) =
                                    (*(uint8_t *)((uint8_t *)req + 7) & 0xF0) | entry_idx;
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
        *(uint32_t *)req = 0;

        /* Set word at offset 2 from per-volume data */
        vol_idx = *(uint8_t *)((uint8_t *)req + 0x1C);
        *(uint16_t *)((uint8_t *)req + 2) = *(uint16_t *)(OS_DISK_DATA + vol_idx * 2 - 2);

        /* Set network info */
        ((uint32_t *)req)[4] = ROUTE_$PORT;
        ((uint32_t *)req)[5] = NODE_$ME;
        ((uint32_t *)req)[6] = 0;
        ((uint32_t *)req)[7] = 0;

        /* Set flags at offset 0x1D */
        *(uint8_t *)((uint8_t *)req + 0x1D) |= 0x40;
        *(uint8_t *)((uint8_t *)req + 0x1C) = entry_idx;
        *(uint8_t *)((uint8_t *)req + 0x1D) = (*(uint8_t *)((uint8_t *)req + 0x1D) & 0xF0) | 1;
        *(uint8_t *)((uint8_t *)req + 1) = (*(uint8_t *)((uint8_t *)req + 1) & 0xF0) | 1;
    }

done:
    ML_$UNLOCK(VTOC_LOCK_ID);
}
