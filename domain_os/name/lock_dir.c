/*
 * NAME_$LOCK_DIR - Acquire directory lock
 *
 * Acquires a lock on a directory for exclusive access.
 * Handles retry logic if the directory is in use, and
 * maps the directory for fast access.
 *
 * Parameters:
 *   dir_uid    - UID of directory to lock
 *   handle_ret - Output: handle for mapped directory
 *   flags      - Lock flags (low word: mode, high word: ACL check flags)
 *   status_ret - Output: status code
 *
 * Original address: 0x00e54854
 * Size: 722 bytes
 *
 * TODO: This is a complex function with A5-relative data access.
 * The full implementation requires understanding of the per-process
 * handle table, mapped info structures, and the MST_$MAPS function.
 */

#include "name/name_internal.h"
#include "time/time.h"
#include "acl/acl.h"
#include "file/file_internal.h"
#include "mst/mst.h"
#include "dir/dir_internal.h"  /* For DAT_00e54b28 */

/* Status codes */
#define file_$object_in_use              0x000F0006
#define status_$naming_directory_locked  0x000E0016

/* Forward declaration */
void NAME_$UNLOCK_DIR(status_$t *status_ret);

/* Extern references for mapped info and lock callback */
extern uint8_t NAME_$WDIR_MAPPED_INFO;
extern uint8_t NAME_$NDIR_MAPPED_INFO;
extern uint8_t NAME_$NODE_MAPPED_INFO;
extern uint8_t NAME_$COM_MAPPED_INFO;
extern uint8_t DAT_00e80288;  /* NODE mapped handle */
extern uint8_t DAT_00e8028e;  /* NODE entry count */
extern uint8_t DAT_00e80270;  /* COM mapped handle */
extern uint8_t DAT_00e80276;  /* COM entry count */
extern uid_t NAME_$WDIR_UID;
extern uid_t NAME_$NDIR_UID;

/* Lock callback data - passed to FILE_$PRIV_LOCK */
extern uint8_t DAT_00e54730;

/* Forward declaration for ACL status conversion */
void NAME_CONVERT_ACL_STATUS(status_$t *status_ret);

void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    uint32_t flags, status_$t *status_ret)
{
    uid_t local_uid;
    int32_t start_time;
    int16_t retry_count;
    uint16_t mode;
    uint16_t acl_flags;
    status_$t local_status;
    uint8_t result_buf[4];
    int32_t handle_slot;
    uint16_t wait_time;

    /* Copy UID locally */
    local_uid.high = dir_uid->high;
    local_uid.low = dir_uid->low;

    /* Extract mode (low 16 bits) and ACL flags (high 16 bits) */
    mode = (uint16_t)(flags & 0xFFFF);
    acl_flags = (uint16_t)((flags >> 16) & 0xFFFF);

    retry_count = 0;
    start_time = TIME_$CLOCKH;

    while (1) {
        /* TODO: Store mode in per-process data at A5+PROC1_$CURRENT*2+0x13e */

        /* TODO: Clear handle pointer at A5+PROC1_$CURRENT*4+0x1bc */

        /* Attempt to acquire lock via FILE_$PRIV_LOCK */
        FILE_$PRIV_LOCK(&local_uid, PROC1_$AS_ID, 0, mode, 0,
                        0x880000,  /* Lock flags */
                        0, 0, 0,
                        &DAT_00e54730,  /* Callback */
                        1,
                        (void *)&handle_slot,  /* Per-process slot */
                        result_buf,
                        status_ret);

        if (*status_ret == status_$ok) {
            /* Lock acquired successfully */
            /* TODO: Store UID in per-process data at A5+PROC1_$CURRENT*8+0x2b8 */
            break;
        }

        if (*status_ret != file_$object_in_use) {
            /* Error other than busy - return it */
            goto error_exit;
        }

        /* Directory is in use - retry logic */
        /* Check if process type is 9 (background) - don't retry */
        /* Also check retry count and elapsed time */
        retry_count++;
        if (retry_count > 0x78 || (TIME_$CLOCKH - start_time) > 0x78) {
            *status_ret = status_$naming_directory_locked;
            goto error_exit;
        }

        /* Wait 0x4000 units before retry */
        wait_time = 0x4000;
        local_status = 0;
        /* TODO: TIME_$WAIT(&ACL_TYPE_FILE, &local_status, (status_$t *)&wait_time); */

        /* Continue retry loop */
    }

    /* Lock acquired - now check ACL if needed */
    if (acl_flags != 0) {
        uint32_t rights_flags = acl_flags;
        ACL_$RIGHTS(&local_uid, &DAT_00e54b28, &rights_flags, &ACL_TYPE_DIR, status_ret);
        ACL_$ENTER_SUPER();
        if (*status_ret != status_$ok) {
            NAME_CONVERT_ACL_STATUS(status_ret);
            NAME_$UNLOCK_DIR(&local_status);
            return;
        }
    } else {
        ACL_$ENTER_SUPER();
    }

    /* Check if this is a known cached directory and return cached handle */
    /* Check NODE_UID */
    if (local_uid.high == NAME_$NODE_UID.high &&
        local_uid.low == NAME_$NODE_UID.low) {
        if ((int8_t)NAME_$NODE_MAPPED_INFO < 0 &&
            *(uint16_t *)(&NAME_$NODE_MAPPED_INFO + 2) == 0 &&
            *(uint16_t *)(&DAT_00e8028e) == 1) {
            *handle_ret = *(uint32_t *)&DAT_00e80288;
            goto check_directory_type;
        }
    }

    /* Check COM_UID */
    if (local_uid.high == NAME_$COM_UID.high &&
        local_uid.low == NAME_$COM_UID.low) {
        if ((int8_t)NAME_$COM_MAPPED_INFO < 0 &&
            *(uint16_t *)(&NAME_$COM_MAPPED_INFO + 2) == 0 &&
            *(uint16_t *)(&DAT_00e80276) == 1) {
            *handle_ret = *(uint32_t *)&DAT_00e80270;
            goto check_directory_type;
        }
    }

    /* Check WDIR_UID for current ASID */
    /* TODO: Check NAME_$WDIR_UID[PROC1_$AS_ID] and NAME_$WDIR_MAPPED_INFO */

    /* Check NDIR_UID for current ASID */
    /* TODO: Check NAME_$NDIR_UID[PROC1_$AS_ID] and NAME_$NDIR_MAPPED_INFO */

    /* Not a cached directory - map it */
    {
        uint8_t map_result[4];
        MST_$MAPS(PROC1_$AS_ID, 0xFF00, &local_uid, 0, 0x10000, 0x16, 0, 0xFF,
                  map_result, status_ret);
        *handle_ret = (uint32_t)(uintptr_t)map_result;  /* A0 return value */
        if ((*status_ret >> 16) != 0) {
            *status_ret |= 0x80000000;  /* Set high bit on error */
            NAME_$UNLOCK_DIR(&local_status);
            return;
        }
    }

check_directory_type:
    /* TODO: Store handle in per-process data at A5+PROC1_$CURRENT*4+0x1bc */

    /* Verify this is actually a directory (type == 1) */
    if (*(int16_t *)(uintptr_t)*handle_ret != 1) {
        *status_ret = status_$naming_bad_directory;
        NAME_$UNLOCK_DIR(&local_status);
        return;
    }

    return;

error_exit:
    if (*status_ret != status_$naming_directory_locked) {
        *status_ret |= 0x80000000;  /* Set high bit */
    }
    ACL_$ENTER_SUPER();
}
