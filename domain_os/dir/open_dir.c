/*
 * dir_$open_dir - Open/lock directory handle
 *
 * Opens a directory by UID and returns a handle. The mode and rights
 * parameters control the access level. Allocates a handle slot, copies
 * the UID, locks the object, validates directory format (type 5),
 * checks ACL rights, and validates pages if dirty.
 *
 * Mode handling:
 *   mode 0: auto-promoted to mode 2 (write) with recovery enabled.
 *           If validation finds naming_internal_error, it's suppressed.
 *           After validation, downgrades back to mode 1 if requested.
 *   mode 1: read access - may need lock upgrade for validation
 *   mode 2: write access
 *
 * Parameters:
 *   uid        - Pointer to directory UID
 *   mode       - Lock mode (0=auto/recovery, 1=read, 2=write)
 *   rights     - ACL rights bitmask to check (0=skip check)
 *   handle_ret - Pointer to handle variable (set on success, cleared on error)
 *   status_ret - Output: status code
 *
 * On failure, the handle is automatically released via dir_$release_handle.
 *
 * Original address: 0x00E4BA02
 * Original size: 546 bytes
 */

#include "dir/dir_internal.h"

/*
 * Constant cells for the ACL_$RIGHTS call at 0x00E4BA8E, addressed with
 * `pea (d,PC)` (PC = instruction address + 2).
 */

/* 0x00E4BC24, byte 0xFF: ACL_$RIGHTS' ignore_super argument (TRUE - the
 * super-user bypass is suppressed).  `pea (0x19a,PC)` at 0x00E4BA88.
 * The word at 0x00E4BC24 reads 0xFF00; only the first byte is used. */

/* 0x00E4B444, word 0x0001: ACL_$RIGHTS' option flags (object type 1,
 * directory).  `pea (-0x636,PC)` at 0x00E4BA78. */

void dir_$open_dir(void *uid, int16_t mode, int16_t rights,
                   void *handle_ret, status_$t *status_ret)
{
    uid_t *uid_ptr = (uid_t *)uid;
    dir_$handle_t **hp = (dir_$handle_t **)handle_ret;
    dir_$handle_t *handle;
    void *page_data;
    uint8_t recovery_flag;
    int16_t effective_mode;
    uint32_t seg_map[8];
    status_$t seg_status;
    uint32_t seg_zero;
    uid_t local_uid;

    /* Mode 0: auto-promote to 2 with recovery flag */
    if (mode == 0) {
        effective_mode = 2;
        recovery_flag = 0xFF;
    } else {
        effective_mode = mode;
        recovery_flag = 0;
    }

    /* Allocate handle slot */
    handle = (dir_$handle_t *)DIR_$ALLOC_HANDLE();
    *hp = handle;
    if (handle == NULL) {
        *status_ret = status_$naming_directory_locked;
        return;
    }

    /* Copy the UID into the handle */
    handle->uid.high = uid_ptr->high;
    handle->uid.low  = uid_ptr->low;

    /* Lock the directory object */
    DIR_$LOCK_OBJ(handle, effective_mode, status_ret);
    if (*status_ret != status_$ok) {
        goto error_release;
    }

    /* Validate the handle */
    DIR_$VALIDATE_HANDLE(handle, effective_mode, status_ret);
    if (*status_ret != status_$ok) {
        goto error_release;
    }

    /* Check ACL rights if requested */
    if (rights != 0) {
        uint32_t rights32 = (uint32_t)(uint16_t)rights;
        ACL_$RIGHTS(uid_ptr,
                    (boolean *)&DIR_$CONST_TRUE_B,
                    &rights32,
                    (int16_t *)&DIR_$CONST_ONE_W,
                    status_ret);
        if (*status_ret != status_$ok) {
            NAME_CONVERT_ACL_STATUS(status_ret);
            /* 0x00E4BAA6: `bclr.b #0x7,(A2)` clears bit 7 of the FIRST byte
             * of the big-endian status longword, i.e. bit 31 of the whole
             * status - not "the low byte" a byte-pointer cast would reach on
             * a little-endian host. */
            *status_ret &= 0x7FFFFFFF;
            if (*status_ret == file_$object_not_found) {
                /* ACL object not found - convert to naming error */
                *status_ret = status_$naming_directory_object_not_found;
            }
            goto error_release;
        }
    }

    /* Map page 0 and verify it's a directory (type 5) */
    page_data = dir_$map_page(handle, 0);
    if ((*((uint8_t *)page_data + 1) & 0x3F) != 5) {
        *status_ret = status_$naming_bad_directory;
        goto error_release;
    }

    /* If directory is small (size <= 0x400), no validation needed */
    if (handle->length <= DIR_PAGE_SIZE) {
        return;
    }

    /* Compute last page index */
    {
        uint32_t total_size = handle->length;
        uint32_t last_page = (total_size >> 10) - 1;

        /* Check if last page is dirty via AST_$GET_SEG_MAP */
        seg_zero = 0;
        local_uid.high = handle->uid.high;
        local_uid.low  = handle->uid.low;

        AST_$GET_SEG_MAP(&local_uid,
                         (uint32_t)((uint16_t)last_page) << 10,
                         seg_zero, 1, 0x20, 2,
                         seg_map, &seg_status);

        if (seg_status != status_$ok) {
            *status_ret = seg_status;
            goto error_release;
        }

        /* Check if the bit for the last page is set in the seg map */
        {
            uint16_t bit_pos = (uint16_t)last_page & 0x1F;
            uint16_t word_idx = bit_pos >> 5;
            if ((seg_map[word_idx] & (1 << bit_pos)) == 0) {
                return;  /* Page not dirty, no validation needed */
            }
        }

        /* Page is dirty - need to validate */

        /* If currently in read mode (1), upgrade to write mode (2) for recovery */
        if (handle->lock_mode == 1) {
            DIR_$UNLOCK_OBJ(handle);
            DIR_$UNMAP_PAGES(handle);

            DIR_$LOCK_OBJ(handle, 2, status_ret);
            if (*status_ret != status_$ok) {
                goto error_release;
            }

            DIR_$VALIDATE_HANDLE(handle, 2, status_ret);
            if (*status_ret != status_$ok) {
                if (*status_ret == status_$naming_vol_mounted_read_only) {
                    *status_ret = status_$naming_cant_recovery_dir_on_ro_vol;
                }
                goto error_release;
            }
        }

        /* Validate pages: crash_flag = ~recovery_flag */
        dir_$validate_pages(handle, (char)~recovery_flag, status_ret);
        if (*status_ret != status_$ok) {
            /* In recovery mode, suppress naming_internal_error */
            if ((int8_t)recovery_flag < 0 &&
                *status_ret == status_$naming_internal_error) {
                *status_ret = status_$ok;
            } else {
                goto error_release;
            }
        }

        /* If original mode was read (1), downgrade back from write */
        if (effective_mode == 1) {
            DIR_$UNLOCK_OBJ(handle);
            DIR_$UNMAP_PAGES(handle);

            DIR_$LOCK_OBJ(handle, 1, status_ret);
            if (*status_ret != status_$ok) {
                goto error_release;
            }

            DIR_$VALIDATE_HANDLE(handle, 1, status_ret);
            if (*status_ret != status_$ok) {
                goto error_release;
            }
        }
    }

    return;

error_release:
    dir_$release_handle(handle_ret);
}
