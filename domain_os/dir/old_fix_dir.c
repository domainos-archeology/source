/*
 * DIR_$OLD_FIX_DIR - Legacy fix/repair directory
 *
 * Attempts to repair a corrupted directory by copying it to a
 * temporary object, reinitializing the original, and replaying
 * all entries from the copy.
 *
 * Original address: 0x00E55C66
 * Original size: 1070 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_FIX_DIR - Legacy fix/repair directory
 *
 * Based on the Ghidra decompilation at 0x00E55C66.
 * Two major paths:
 *
 * Path 1 (directory is OK): status from FUN_00e54854 is OK
 *   1. Create a temporary file via FILE_$PRIV_CREATE
 *   2. Lock the temp file and map it
 *   3. Copy all data from the directory to the temp
 *   4. Truncate the original directory
 *   5. Reinitialize the directory buffer via FUN_00e544b0
 *   6. Unlock temp, copy info block, then replay all entries:
 *      - Type 1 entries: add via OLD_ADDU or OLD_ROOT_ADDU
 *      - Type 3 (link) entries: read link, then add via OLD_ADD_LINKU
 *   7. Clean up temp file
 *
 * Path 2 (directory is bad): status is naming_bad_directory
 *   1. Map the directory directly
 *   2. Lock it, check version
 *   3. If version < 2, reinitialize the buffer
 *   4. Get attributes to restore parent UID
 *   5. Unlock and unmap
 *
 * Parameters:
 *   dir_uid    - UID of directory to fix
 *   status_ret - Output: status code
 */
void DIR_$OLD_FIX_DIR(uid_t *dir_uid, status_$t *status_ret)
{
    uid_t local_dir;
    uid_t temp_uid;
    uint32_t handle;
    uint32_t lock_handle;
    uint16_t lock_result;
    void *mapped_ptr;
    status_$t status;
    status_$t cleanup_status;
    uint8_t dtv_buf[8];
    uint8_t map_result[4];
    int8_t did_lock;
    int8_t did_map;
    int8_t need_unlock;

    /* Info block data */
    uint8_t info_buf[40];
    int16_t info_len[2];

    /* Directory read state */
    int32_t count;
    int32_t continuation;
    uint8_t read_entries[256];
    int16_t entry_type;
    int16_t entry_name_len;
    char entry_name[258];
    uint8_t entry_uid_buf[8];
    uint8_t entry_link_buf[256];
    int16_t link_len[2];
    uid_t entry_uid;

    /* Make local copy of directory UID */
    local_dir.high = dir_uid->high;
    local_dir.low = dir_uid->low;

    did_lock = 0;
    did_map = 0;
    need_unlock = -1;  /* true = need to unlock on exit */

    /* Try to acquire directory lock */
    FUN_00e54854(&local_dir, &handle, 0x40002, status_ret);
    mapped_ptr = (void *)handle;

    if (*status_ret == status_$ok) {
        /* Path 1: Directory structure is accessible */

        /* Create temporary file */
        FILE_$PRIV_CREATE(1, &UID_$NIL, &local_dir, &temp_uid,
                          0, 0, 0, status_ret);
        did_lock = 0;
        if (*status_ret != status_$ok) {
            goto cleanup;
        }

        /* Lock the temp file */
        FILE_$PRIV_LOCK(&temp_uid, PROC1_$AS_ID, 0, 4, 0,
                        0x880000, 0, 0, 0,
                        &DAT_00e54730, 1, &lock_handle, &lock_result,
                        status_ret);
        did_lock = -1;
        if (*status_ret != status_$ok) {
            goto cleanup;
        }

        /* Map the temp file */
        MST_$MAPS(PROC1_$AS_ID, 0xFF00, &temp_uid, 0, 0x10000,
                  0x16, 0, 0xFF, map_result, status_ret);
        if (*status_ret != status_$ok) {
            goto cleanup;
        }
        did_map = -1;

        /* Copy directory data to temp - 0x3FD8 longwords = ~64KB */
        {
            uint32_t *src = (uint32_t *)handle;
            uint32_t *dst = (uint32_t *)mapped_ptr;
            int16_t words = 0x3FD6;
            int16_t j;
            for (j = 0; j <= words; j++) {
                dst[j] = src[j];
            }
            /* Copy final 2 bytes */
            *((uint16_t *)&dst[words + 1]) = *((uint16_t *)&src[words + 1]);
        }

        /* Truncate the original directory */
        FILE_$TRUNCATE(&local_dir, &DAT_00e54730, status_ret);
        if (*status_ret != status_$ok) {
            goto cleanup;
        }

        /* Reinitialize the directory buffer */
        FUN_00e544b0((void *)handle);

        /* Release directory lock */
        FUN_00e54734(status_ret);
        if (*status_ret != status_$ok) {
            goto cleanup;
        }

        need_unlock = 0;  /* No longer need to unlock */

        /* Unlock temp file */
        FILE_$PRIV_UNLOCK(&temp_uid, (uint16_t)lock_handle,
                          0x00040000 | PROC1_$AS_ID,
                          1 << 16, 0, 0, dtv_buf, status_ret);
        if (*status_ret != status_$ok) {
            goto cleanup;
        }

        /* Unmap temp file */
        MST_$UNMAP_PRIVI(1, &temp_uid, mapped_ptr, 0x10000,
                         PROC1_$AS_ID, status_ret);
        if (*status_ret != status_$ok) {
            goto cleanup;
        }
        did_map = 0;

        /* Copy info block from temp to original */
        DIR_$OLD_READ_INFOBLK(&temp_uid, info_buf, &DAT_00e56096,
                              info_len, status_ret);
        if (*status_ret == status_$ok) {
            DIR_$OLD_WRITE_INFOBLK(&local_dir, info_buf, info_len, status_ret);
        }

        /* Replay all entries */
        continuation = 1;
        while (1) {
            DIR_$OLD_DIR_READU(&temp_uid, &continuation,
                               &DAT_00e560a2, &DAT_00e5609a,
                               read_entries, &count, status_ret);
            if (*status_ret != status_$ok || count == 0) {
                break;
            }

            /* Get entry type from read data */
            entry_type = *((int16_t *)(read_entries + 2));
            entry_name_len = *((int16_t *)(read_entries + 4));
            /* TODO: Verify exact entry field offsets from Ghidra */

            if (entry_type == 1) {
                /* Regular entry - re-add */
                if (NAME_$ROOT_UID.high == local_dir.high &&
                    NAME_$ROOT_UID.low == local_dir.low) {
                    DIR_$OLD_ROOT_ADDU(&local_dir, entry_name, &entry_name_len,
                                       (uid_t *)entry_uid_buf, (uint32_t *)dtv_buf,
                                       &status);
                } else {
                    DIR_$OLD_ADDU(&local_dir, entry_name, &entry_name_len,
                                  (uid_t *)entry_uid_buf, &status);
                }
            } else if (entry_type == 3) {
                /* Link entry - read link then re-add */
                DIR_$OLD_READ_LINKU((int16_t)(uintptr_t)&temp_uid,
                                    (int16_t)(uintptr_t)entry_name,
                                    (uint16_t *)&entry_name_len,
                                    (int16_t)(uintptr_t)entry_link_buf,
                                    link_len, &entry_uid, &status);
                if (status == status_$ok) {
                    DIR_$OLD_ADD_LINKU(&local_dir, entry_name, &entry_name_len,
                                       entry_link_buf, (uint16_t *)link_len,
                                       &status);
                }
            }
        }
        goto cleanup;

    } else if (*status_ret == status_$naming_bad_directory) {
        /* Path 2: Directory structure is corrupted */

        /* Map the directory directly */
        MST_$MAPS(PROC1_$AS_ID, 0xFF00, &local_dir, 0, 0x10000,
                  0x16, 0, 0xFF, map_result, status_ret);
        mapped_ptr = (void *)map_result;
        if (*status_ret != status_$ok) {
            goto done;
        }

        /* Lock the directory */
        FILE_$PRIV_LOCK(&local_dir, PROC1_$AS_ID, 0, 4, 0,
                        0x880000, 0, 0, 0,
                        &DAT_00e54730, 1, &lock_handle, &lock_result,
                        status_ret);
        if (*status_ret != status_$ok) {
            goto unmap_and_done;
        }

        /* Check version */
        if (*((uint16_t *)mapped_ptr) < 2) {
            /* Old version - reinitialize */
            FUN_00e544b0(mapped_ptr);

            /* Get attributes to restore parent UID */
            {
                uint32_t attr_high, attr_low;
                FILE_$GET_ATTRIBUTES(&local_dir, &DAT_00e56098,
                                     &DAT_00e56094, info_buf,
                                     info_buf + 0x3C, &status);
                if (status == status_$ok) {
                    /* Restore parent UID at offset 0x0E in mapped buffer */
                    *((uint32_t *)((uint8_t *)mapped_ptr + 0x0E)) =
                        *((uint32_t *)(info_buf + 0x30));
                    *((uint32_t *)((uint8_t *)mapped_ptr + 0x12)) =
                        *((uint32_t *)(info_buf + 0x34));
                }
            }
            *status_ret = status_$ok;
        } else {
            *status_ret = status_$naming_bad_directory;
        }

        /* Unlock */
        FILE_$PRIV_UNLOCK(&local_dir, (uint16_t)lock_handle,
                          0x00040000 | PROC1_$AS_ID,
                          0, 0, 0, dtv_buf, &status);
        if (*status_ret == status_$ok) {
            *status_ret = status;
        }

unmap_and_done:
        /* Unmap */
        MST_$UNMAP_PRIVI(1, &local_dir, mapped_ptr, 0x10000,
                         PROC1_$AS_ID, &status);
        if (*status_ret == status_$ok) {
            *status_ret = status;
        }
        goto done;
    }

cleanup:
    /* Cleanup for Path 1 */
    if (did_map < 0) {
        MST_$UNMAP(&temp_uid, 0, &DAT_00e5609e, &status);
    }
    if (did_lock < 0) {
        FILE_$SET_REFCNT(&temp_uid, &DAT_00e54730, &status);
    }
    if (need_unlock < 0) {
        FUN_00e54734(&status);
    }

done:
    ACL_$EXIT_SUPER();
}
