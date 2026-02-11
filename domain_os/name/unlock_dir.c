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
 * TODO: This function uses A5-relative data for per-process state.
 * Full implementation requires understanding of the handle table layout.
 */

#include "name/name_internal.h"
#include "file/file_internal.h"
#include "mst/mst.h"

/* Extern references for mapped info */
extern uint8_t NAME_$WDIR_MAPPED_INFO;
extern uint8_t NAME_$NDIR_MAPPED_INFO;
extern uint8_t NAME_$NODE_MAPPED_INFO;
extern uint8_t NAME_$COM_MAPPED_INFO;
extern uint8_t DAT_00e80288;  /* NODE mapped handle */
extern uint8_t DAT_00e80270;  /* COM mapped handle */
extern uint8_t DAT_00e80818;  /* WDIR mapped handle offset */
extern uint8_t DAT_00e802a8;  /* NDIR mapped handle offset */

void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    uid_t local_uid;
    int32_t handle;
    status_$t unlock_status;
    int16_t asid_shift;
    uint8_t result_buf[12];

    /* Get stored UID from per-process data */
    /* A5 + PROC1_$CURRENT*8 + 0x2b8 */
    /* TODO: Implement proper A5-relative data access */
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

    asid_shift = PROC1_$AS_ID << 4;

    /* Check if handle matches a cached directory - don't unmap cached dirs */
    /* Check WDIR */
    if ((int8_t)(&NAME_$WDIR_MAPPED_INFO)[asid_shift] < 0 &&
        handle == (int32_t)*(uint32_t *)(&DAT_00e80818 + asid_shift)) {
        *status_ret = status_$ok;
    }
    /* Check NDIR */
    else if ((int8_t)(&NAME_$NDIR_MAPPED_INFO)[asid_shift] < 0 &&
             handle == (int32_t)*(uint32_t *)(&DAT_00e802a8 + asid_shift)) {
        *status_ret = status_$ok;
    }
    /* Check NODE */
    else if ((int8_t)NAME_$NODE_MAPPED_INFO < 0 &&
             handle == (int32_t)*(uint32_t *)&DAT_00e80288) {
        *status_ret = status_$ok;
    }
    /* Check COM */
    else if ((int8_t)NAME_$COM_MAPPED_INFO < 0 &&
             handle == (int32_t)*(uint32_t *)&DAT_00e80270) {
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

        /* TODO: Read actual values from per-process data */
        lock_handle = 0;  /* Placeholder */
        mode = 0;         /* Placeholder */

        FILE_$PRIV_UNLOCK(&local_uid, (int16_t)lock_handle,
                          PROC1_$AS_ID | (mode << 16),
                          0, 0, 0, result_buf, &unlock_status);
    }

    /* Clear the stored UID to indicate no longer locked */
    /* A5 + PROC1_$CURRENT*8 + 0x2b8 = 0 */
    /* TODO: Implement proper write to per-process data */

    /* Use unlock status if primary status was OK */
    if ((*status_ret >> 16) == 0) {
        *status_ret = unlock_status;
    }

    /* Set error bit if not OK */
    if (*status_ret != status_$ok) {
        *status_ret |= 0x80000000;
    }
}
