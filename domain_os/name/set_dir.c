/*
 * NAME Directory Setter Functions
 *
 * Functions to set the working directory and naming directory.
 * These are per-process settings stored in the NAME data area.
 *
 * Original addresses:
 *   NAME_$SET_WDIR:   0x00e4a3d0 (56 bytes)
 *   NAME_$SET_WDIRUS: 0x00e58670 (294 bytes)
 *   NAME_$SET_NDIRUS: 0x00e587a0 (286 bytes)
 */

#include "name/name_internal.h"

/* NAME_CONVERT_ACL_STATUS and name_$unmap_dir_buffers are declared in
 * name/name.h and name/name_internal.h.  Per-ASID data lives in NAME_$DATA
 * (0xE80264): ndir_uid[] +0x3E0, wdir_uid[] +0x950, ndir_mapped_info[] +0x040,
 * wdir_mapped_info[] +0x5B0. */

/*
 * NAME_$SET_WDIR - Set working directory by pathname
 *
 * Resolves the given pathname to a UID and sets it as the working directory.
 *
 * Parameters:
 *   path       - Pathname of new working directory
 *   path_len   - Pointer to pathname length
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a3d0
 */
void NAME_$SET_WDIR(char *path, int16_t *path_len, status_$t *status_ret)
{
    uid_t wdir_uid;

    NAME_$RESOLVE(path, path_len, &wdir_uid, status_ret);

    if (*status_ret == status_$ok) {
        NAME_$SET_WDIRUS(&wdir_uid, status_ret);
    }
}

/*
 * NAME_$SET_WDIRUS - Set working directory by UID
 *
 * Sets the working directory for the current process using a UID.
 * Performs ACL check to verify the caller has access.
 *
 * Parameters:
 *   uidp       - UID of new working directory
 *   status_ret - Output: status code
 *
 * Original address: 0x00e58670
 */
void NAME_$SET_WDIRUS(uid_t *uidp, status_$t *status_ret)
{
    uid_t *current_wdir;

    /* Slot for current ASID (8 bytes per UID) */
    current_wdir = &NAME_$DATA.wdir_uid[PROC1_$AS_ID];

    /* If already set to this UID, nothing to do */
    if (uidp->high == current_wdir->high && uidp->low == current_wdir->low) {
        *status_ret = status_$ok;
        return;
    }

    /* Enter supervisor mode for ACL check */
    ACL_$ENTER_SUPER();

    /* Check access rights */
    /* TODO(source-0bo): The exact ACL parameters need more analysis */
    if (ACL_$RIGHTS(uidp, NULL, NULL, NULL, status_ret) == 0) {
        NAME_CONVERT_ACL_STATUS(status_ret);
    } else {
        /* Unmap old directory (16 bytes per mapped info) */
        name_$unmap_dir_buffers(PROC1_$AS_ID, &NAME_$DATA.wdir_mapped_info[PROC1_$AS_ID]);

        /* Map new directory */
        name_$map_dir(uidp, PROC1_$AS_ID,
                     &NAME_$DATA.wdir_mapped_info[PROC1_$AS_ID], status_ret);

        if (*status_ret == status_$ok) {
            /* Update the working directory UID */
            current_wdir->high = uidp->high;
            current_wdir->low = uidp->low;
            *status_ret = status_$ok;
        } else {
            /* Set high bit to indicate error during mapping */
            *status_ret |= 0x80000000;  /* high bit of the first byte (m68k big-endian) */
        }
    }

    ACL_$EXIT_SUPER();

    /* Audit logging if enabled */
    /* TODO(source-0bo): Implement audit logging when AUDIT subsystem is available */
}

/*
 * NAME_$SET_NDIRUS - Set naming directory by UID
 *
 * Sets the naming directory for the current process using a UID.
 * Performs ACL check to verify the caller has access.
 *
 * Parameters:
 *   uidp       - UID of new naming directory
 *   status_ret - Output: status code
 *
 * Original address: 0x00e587a0
 */
void NAME_$SET_NDIRUS(uid_t *uidp, status_$t *status_ret)
{
    uid_t *current_ndir;

    /* Slot for current ASID (8 bytes per UID) */
    current_ndir = &NAME_$DATA.ndir_uid[PROC1_$AS_ID];

    /* If already set to this UID, nothing to do */
    if (uidp->high == current_ndir->high && uidp->low == current_ndir->low) {
        *status_ret = status_$ok;
        return;
    }

    /* Enter supervisor mode for ACL check */
    ACL_$ENTER_SUPER();

    /* Check access rights */
    if (ACL_$RIGHTS(uidp, NULL, NULL, NULL, status_ret) == 0) {
        NAME_CONVERT_ACL_STATUS(status_ret);
    } else {
        /* Unmap old directory (16 bytes per mapped info) */
        name_$unmap_dir_buffers(PROC1_$AS_ID, &NAME_$DATA.ndir_mapped_info[PROC1_$AS_ID]);

        /* Map new directory */
        name_$map_dir(uidp, PROC1_$AS_ID,
                     &NAME_$DATA.ndir_mapped_info[PROC1_$AS_ID], status_ret);

        if (*status_ret == status_$ok) {
            /* Update the naming directory UID */
            current_ndir->high = uidp->high;
            current_ndir->low = uidp->low;
            *status_ret = status_$ok;
        } else {
            /* Set high bit to indicate error during mapping */
            *status_ret |= 0x80000000;  /* high bit of the first byte (m68k big-endian) */
        }
    }

    ACL_$EXIT_SUPER();

    /* Audit logging if enabled */
    /* TODO(source-0bo): Implement audit logging when AUDIT subsystem is available */
}
