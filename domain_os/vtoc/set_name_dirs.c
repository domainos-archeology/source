/*
 * VTOC_$SET_NAME_DIRS - Set name directory UIDs
 *
 * Original address: 0x00e39486
 * Size: 102 bytes
 *
 * Updates the name directory UIDs for a volume.
 */

#include "vtoc/vtoc_internal.h"

void VTOC_$SET_NAME_DIRS(int16_t vol_idx, uid_t *dir1_uid, uid_t *dir2_uid,
                         status_$t *status_ret)
{
    int16_t vol_offset;
    vtoc_$lookup_req_t req;

    vol_offset = vol_idx * 100;

    /* Set up lookup request for dir1 */
    req.uid = *dir1_uid;
    *(uint8_t *)((uint8_t *)&req + 0x1C) = (uint8_t)vol_idx;
    req.block_hint = *(uint32_t *)(OS_DISK_DATA + vol_offset - 0x4C);

    /* Look up dir1 */
    VTOC_$LOOKUP(&req, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /* Set up lookup request for dir2 */
    req.uid = *dir2_uid;
    req.block_hint = *(uint32_t *)(OS_DISK_DATA + vol_offset - 0x48);

    /* Look up dir2 */
    VTOC_$LOOKUP(&req, status_ret);
}
