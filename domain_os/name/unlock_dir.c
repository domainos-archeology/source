/*
 * NAME_$UNLOCK_DIR - Release directory lock
 *
 * Releases a directory lock previously acquired with NAME_$LOCK_DIR.
 * Unmaps the directory if it was mapped, and releases the file lock.
 *
 * Parameters:
 *   status_ret - Output: status code
 *
 * Original address: 0x00e54734
 * Size: 288 bytes
 *
 * TODO(source-0i3): This function uses A5-relative data for per-process state.
 * Full implementation requires understanding of the handle table layout.
 */

#include "name/name_internal.h"
#include "file/file_internal.h"
#include "mst/mst.h"

/*
 * Mapped info blocks are declared in name/name.h (NAME_$DATA).  The
 * historical labels DAT_00e80288 / DAT_00e80270 / DAT_00e80818 / DAT_00e802a8
 * are the .first_base field of the NODE / COM / WDIR[0] / NDIR[0] mapped
 * info blocks respectively.
 */

void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    uid_t local_uid;
    int32_t handle;
    status_$t unlock_status;
    uint8_t result_buf[12];

    /* Get stored UID from per-process data */
    /* A5 + PROC1_$CURRENT*8 + 0x2b8 */
    /* TODO(source-0i3): Implement proper A5-relative data access */
    local_uid.high = 0;  /* Placeholder - should read from per-process data */
    local_uid.low = 0;

    /* Get stored handle from per-process data */
    /* A5 + PROC1_$CURRENT*4 + 0x1bc */
    handle = 0;  /* Placeholder - should read from per-process data */

    if (local_uid.high == 0 && local_uid.low == 0) {
        /* No directory was locked */
        *status_ret = status_$ok;
        return;
    }

    /* Check if handle matches a cached directory - don't unmap cached dirs */
    /* Check WDIR (per-ASID slot, 16 bytes each) */
    if (NAME_$DATA.wdir_mapped_info[PROC1_$AS_ID].active < 0 &&
        handle == (int32_t)NAME_$DATA.wdir_mapped_info[PROC1_$AS_ID].first_base) {
        *status_ret = status_$ok;
    }
    /* Check NDIR */
    else if (NAME_$DATA.ndir_mapped_info[PROC1_$AS_ID].active < 0 &&
             handle == (int32_t)NAME_$DATA.ndir_mapped_info[PROC1_$AS_ID].first_base) {
        *status_ret = status_$ok;
    }
    /* Check NODE */
    else if (NAME_$NODE_MAPPED_INFO.active < 0 &&
             handle == (int32_t)NAME_$NODE_MAPPED_INFO.first_base) {
        *status_ret = status_$ok;
    }
    /* Check COM */
    else if (NAME_$COM_MAPPED_INFO.active < 0 &&
             handle == (int32_t)NAME_$COM_MAPPED_INFO.first_base) {
        *status_ret = status_$ok;
    }
    /* Handle is null */
    else if (handle == 0) {
        *status_ret = status_$ok;
    }
    /* Not a cached directory - unmap it */
    else {
        MST_$UNMAP_PRIVI(3, (uint64_t *)&local_uid, handle, 0x10000,
                         PROC1_$AS_ID, status_ret);
    }

    /* Release the file lock via FILE_$PRIV_UNLOCK */
    /* Parameters come from per-process data at various A5 offsets */
    {
        int32_t lock_handle;  /* From A5 + PROC1_$CURRENT*4 + 0x3c */
        uint16_t mode;        /* From A5 + PROC1_$CURRENT*2 + 0x13e */

        /* TODO(source-0i3): Read actual values from per-process data */
        lock_handle = 0;  /* Placeholder */
        mode = 0;         /* Placeholder */

        FILE_$PRIV_UNLOCK(&local_uid, (int16_t)lock_handle,
                          PROC1_$AS_ID | (mode << 16),
                          0, 0, 0, result_buf, &unlock_status);
    }

    /* Clear the stored UID to indicate no longer locked */
    /* A5 + PROC1_$CURRENT*8 + 0x2b8 = 0 */
    /* TODO(source-0i3): Implement proper write to per-process data */

    /* Use unlock status if primary status was OK */
    if ((*status_ret >> 16) == 0) {
        *status_ret = unlock_status;
    }

    /* Set error bit if not OK */
    if (*status_ret != status_$ok) {
        *status_ret |= 0x80000000;
    }
}
