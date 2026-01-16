/*
 * VTOC_$GET_UID - Get UID from VTOCE location
 *
 * Original address: 0x00e391f2
 * Size: 508 bytes
 *
 * Retrieves the UID of a VTOCE given its block and entry index.
 */

#include "vtoc/vtoc_internal.h"

void VTOC_$GET_UID(int16_t *vol_idx_ptr, uint16_t *vtoc_idx_ptr, uint32_t *entry_idx_ptr,
                   uid_t *uid_ret, status_$t *status_ret)
{
    int16_t vol_idx;
    int16_t vol_offset;
    uint16_t vtoc_idx;
    uint16_t entry_idx;
    int16_t i;
    int16_t num_partitions;
    uint32_t block;
    uint32_t *buf;
    uid_t local_uid;
    uint16_t bucket_idx;
    status_$t local_status;

    vol_idx = *vol_idx_ptr;
    vtoc_idx = *vtoc_idx_ptr;
    entry_idx = (uint16_t)*entry_idx_ptr;

    /* Select UID type based on format */
    if (vtoc_$data.format[vol_idx] < 0) {
        /* New format - use bucket UID */
        num_partitions = 9;
        bucket_idx = vtoc_idx & 3;
        vtoc_idx >>= 2;
        local_uid = VTOC_BKT_$UID;
    } else {
        /* Old format - use VTOC UID */
        num_partitions = 7;
        local_uid = VTOC_$UID;
        bucket_idx = vtoc_idx;
    }

    ML_$LOCK(VTOC_LOCK_ID);

    vol_offset = vol_idx * 100;

    if (vtoc_$data.mounted[vol_idx] >= 0) {
        *status_ret = status_$VTOC_not_mounted;
        goto done;
    }

    /* Calculate starting block from partition table */
    block = 0;
    for (i = num_partitions; i >= 0; i--) {
        uint16_t part_count = *(uint16_t *)(OS_DISK_DATA + vol_offset - 0x3C);
        if (vtoc_idx < part_count) {
            /* Found the partition - calculate block */
            uint32_t part_base = *(uint32_t *)(OS_DISK_DATA + vol_offset - 0x3A + i * 6);
            block = part_base + vtoc_idx;
            break;
        }
        vtoc_idx -= part_count;
    }

    buf = NULL;

    while (block != 0) {
        /* Release previous buffer if any */
        if (buf != NULL) {
            DBUF_$SET_BUFF(buf, BAT_BUF_CLEAN, &local_status);
        }

        /* Get the block */
        buf = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, block, &local_uid, block, 0, status_ret);
        if (*status_ret != status_$ok) {
            goto cleanup;
        }

        if (vtoc_$data.format[vol_idx] < 0) {
            /* New format - bucket lookup */
            uint32_t *bucket_entry = buf + (uint32_t)bucket_idx * 0x3E;
            bucket_idx = *(uint16_t *)(bucket_entry + 1);

            if (entry_idx < 0x14) {
                /* Get UID from slot */
                local_uid.high = bucket_entry[(uint32_t)entry_idx * 3 + 2];
                local_uid.low = bucket_entry[(uint32_t)entry_idx * 3 + 3];

                /* Check if valid */
                if (bucket_entry[(uint32_t)entry_idx * 3 + 4] != 0) {
                    /* Verify against current VTOCE location */
                    uint32_t curr_vtoce = *(uint32_t *)(OS_DISK_DATA + vol_offset - 0x44);
                    if (bucket_entry[(uint32_t)entry_idx * 3 + 4] != curr_vtoce) {
                        goto cleanup;
                    }
                }

                *status_ret = status_$no_UID;
                goto cleanup;
            }

            entry_idx -= 0x14;
            block = *bucket_entry;

        } else {
            /* Old format - direct lookup */
            if (entry_idx < 5) {
                /* Get UID from entry */
                local_uid.high = buf[(uint32_t)entry_idx * 0x33 + 2];
                local_uid.low = buf[(uint32_t)entry_idx * 0x33 + 3];

                /* Check if entry is valid */
                if (*(int16_t *)((uint8_t *)buf + (uint32_t)entry_idx * 0xCC + 6) >= 0) {
                    *status_ret = status_$no_UID;
                    goto cleanup;
                }

                /* Verify against current VTOCE location */
                {
                    uint32_t curr_vtoce = *(uint32_t *)(OS_DISK_DATA + vol_offset - 0x44);
                    if ((curr_vtoce >> 4) == block) {
                        if ((curr_vtoce & 0x0F) == entry_idx) {
                            goto cleanup;
                        }
                    }
                }
                goto cleanup;
            }

            entry_idx -= 5;
            block = *buf;
        }
    }

    *status_ret = status_$VTOC_not_found;

cleanup:
    if (buf != NULL) {
        DBUF_$SET_BUFF(buf, BAT_BUF_CLEAN, &local_status);
    }

done:
    ML_$UNLOCK(VTOC_LOCK_ID);

    uid_ret->high = local_uid.high;
    uid_ret->low = local_uid.low;
}
