/*
 * VTOC_$GET_NAME_DIRS - Get name directory UIDs
 *
 * Original address: 0x00e393ee
 * Size: 152 bytes
 *
 * Retrieves the UIDs of the two name directory objects for a volume.
 */

#include "vtoc/vtoc_internal.h"

void VTOC_$GET_NAME_DIRS(int16_t vol_idx, uid_t *dir1_uid, uid_t *dir2_uid,
                         status_$t *status_ret)
{
    int16_t vol_offset;
    vtoc_$lookup_req_t req;
    vtoce_$result_t result;

    vol_offset = vol_idx * 100;

    /* Check if name_dir2 exists */
    if (*(uint32_t *)(OS_DISK_DATA + vol_offset - 0x48) != 0) {
        /* Set up lookup request for name_dir2 */
        req.uid = UID_$NIL;
        req.block_hint = *(uint32_t *)(OS_DISK_DATA + vol_offset - 0x48);
        *(uint8_t *)((uint8_t *)&req + 0x1C) = (uint8_t)vol_idx;

        /* Read the VTOCE */
        VTOCE_$READ(&req, &result, status_ret);

        if (*status_ret != status_$ok) {
            return;
        }

        /* Extract UID from result (at offset 0x04 in new format VTOCE) */
        dir2_uid->high = *(uint32_t *)(result.data + 0x04);
        dir2_uid->low = *(uint32_t *)(result.data + 0x08);

        /* Check if name_dir1 exists */
        if (*(uint32_t *)(OS_DISK_DATA + vol_offset - 0x4C) != 0) {
            req.block_hint = *(uint32_t *)(OS_DISK_DATA + vol_offset - 0x4C);

            /* Read the VTOCE for dir1 */
            VTOCE_$READ(&req, &result, status_ret);

            /* Extract UID */
            dir1_uid->high = *(uint32_t *)(result.data + 0x04);
            dir1_uid->low = *(uint32_t *)(result.data + 0x08);

            return;
        }
    }

    *status_ret = status_$no_UID;
}
