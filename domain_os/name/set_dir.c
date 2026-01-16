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

/* External references */
extern int16_t PROC1_$AS_ID;           /* 0x00e2060a - current address space ID */
extern boolean AUDIT_$ENABLED;          /* 0x00e2e09e - audit logging enabled */

/* Forward declarations for ACL subsystem */
extern void ACL_$ENTER_SUPER(void);
extern void ACL_$EXIT_SUPER(void);
extern int ACL_$RIGHTS(uid_t *uid, void *param2, void *param3, void *param4, status_$t *status);

/* Forward declarations for AUDIT subsystem */
extern void AUDIT_$LOG_EVENT(void *event, uint16_t *result, int16_t status,
                             int16_t uid_param, uint16_t event_type);

/* Internal helper to convert ACL status - defined elsewhere */
extern void NAME_CONVERT_ACL_STATUS(status_$t *status);

/* Internal helper to unmap directory */
extern void FUN_00e58560(int16_t asid, void *mapped_info);

/*
 * Per-ASID data offsets (relative to name_$data_base at 0xE80264)
 */
#define NAME_DATA_NDIR_UID_BASE_OFF         0x3E0
#define NAME_DATA_WDIR_UID_BASE_OFF         0x950
#define NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF 0x040
#define NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF 0x5B0

/* Base pointer for NAME data area */
extern uid_t NAME_$NODE_DATA_UID;  /* At 0xE80264 - base of name data */

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
    int16_t uid_offset;
    int16_t mapped_offset;
    char *base = (char *)&NAME_$NODE_DATA_UID;
    uid_t *current_wdir;

    /* Calculate offset for current ASID */
    uid_offset = PROC1_$AS_ID << 3;      /* 8 bytes per UID */
    current_wdir = (uid_t *)(base + NAME_DATA_WDIR_UID_BASE_OFF + uid_offset);

    /* If already set to this UID, nothing to do */
    if (uidp->high == current_wdir->high && uidp->low == current_wdir->low) {
        *status_ret = status_$ok;
        return;
    }

    /* Enter supervisor mode for ACL check */
    ACL_$ENTER_SUPER();

    /* Check access rights */
    /* TODO: The exact ACL parameters need more analysis */
    if (ACL_$RIGHTS(uidp, NULL, NULL, NULL, status_ret) == 0) {
        NAME_CONVERT_ACL_STATUS(status_ret);
    } else {
        /* Unmap old directory */
        mapped_offset = PROC1_$AS_ID << 4;  /* 16 bytes per mapped info */
        FUN_00e58560(PROC1_$AS_ID, base + NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF + mapped_offset);

        /* Map new directory */
        FUN_00e58488(uidp, PROC1_$AS_ID,
                     base + NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF + mapped_offset, status_ret);

        if (*status_ret == status_$ok) {
            /* Update the working directory UID */
            current_wdir->high = uidp->high;
            current_wdir->low = uidp->low;
            *status_ret = status_$ok;
        } else {
            /* Set high bit to indicate error during mapping */
            *(uint8_t *)status_ret |= 0x80;
        }
    }

    ACL_$EXIT_SUPER();

    /* Audit logging if enabled */
    /* TODO: Implement audit logging when AUDIT subsystem is available */
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
    int16_t uid_offset;
    int16_t mapped_offset;
    char *base = (char *)&NAME_$NODE_DATA_UID;
    uid_t *current_ndir;

    /* Calculate offset for current ASID */
    uid_offset = PROC1_$AS_ID << 3;      /* 8 bytes per UID */
    current_ndir = (uid_t *)(base + NAME_DATA_NDIR_UID_BASE_OFF + uid_offset);

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
        /* Unmap old directory */
        mapped_offset = PROC1_$AS_ID << 4;  /* 16 bytes per mapped info */
        FUN_00e58560(PROC1_$AS_ID, base + NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF + mapped_offset);

        /* Map new directory */
        FUN_00e58488(uidp, PROC1_$AS_ID,
                     base + NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF + mapped_offset, status_ret);

        if (*status_ret == status_$ok) {
            /* Update the naming directory UID */
            current_ndir->high = uidp->high;
            current_ndir->low = uidp->low;
            *status_ret = status_$ok;
        } else {
            /* Set high bit to indicate error during mapping */
            *(uint8_t *)status_ret |= 0x80;
        }
    }

    ACL_$EXIT_SUPER();

    /* Audit logging if enabled */
    /* TODO: Implement audit logging when AUDIT subsystem is available */
}
