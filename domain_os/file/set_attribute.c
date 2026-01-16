/*
 * FILE_$SET_ATTRIBUTE - Set a file attribute
 *
 * Core function for setting file attributes. Handles both local and remote
 * files, checking ACL permissions as needed.
 *
 * Original address: 0x00E5D242
 */

#include "file/file_internal.h"

/* Remote file access denied status codes */
#define status_$no_rights                              0x000F0010
#define status_$file_bad_reply_received_from_remote    0x000F0003
#define status_$insufficient_rights                    0x000F0011

/*
 * FILE_$SET_ATTRIBUTE
 *
 * Sets a file attribute, handling both local and remote files.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   attr_id    - Attribute ID to set
 *   value      - Pointer to attribute value
 *   flags      - Operation flags (low 16 bits = required rights, high 16 bits = option flags)
 *   status_ret - Receives operation status
 *
 * Flow:
 * 1. Get file location via AST_$GET_LOCATION
 * 2. If file is remote (bit 7 of location flags set):
 *    a. Get extended SID via ACL_$GET_EXSID
 *    b. Check if file is in local cache via HINT_$LOOKUP_CACHE
 *    c. If not cached locally, try REM_FILE_$FILE_SET_ATTRIB
 *    d. On success, update local AST cache via AST_$SET_ATTR
 * 3. If rights required, check ACL via ACL_$RIGHTS
 * 4. Finally call AST_$SET_ATTRIBUTE to set the attribute
 */
void FILE_$SET_ATTRIBUTE(uid_t *file_uid, int16_t attr_id, void *value,
                         uint32_t flags, status_$t *status_ret)
{
    status_$t location_status;

    /*
     * The original function uses a complex stack layout where the UID is
     * stored at a fixed location and AST_$GET_LOCATION populates additional
     * info. The remote flag is checked at offset -0x6B relative to frame.
     *
     * For simplicity, we use a struct to hold the location info.
     */
    struct {
        uint32_t uid_high;          /* -0x80: UID high */
        uint32_t uid_low;           /* -0x7C: UID low */
        uint8_t  location[13];      /* -0x78 to -0x6B: Location info */
        uint8_t  remote_flags;      /* -0x6B: Flags byte (bit 7 = remote) */
    } lookup_context;

    uint32_t uid_info[2];           /* Output from AST_$GET_LOCATION (8 bytes) */
    uint32_t vol_uid_out;           /* Volume UID output */

    /* For remote files */
    uint8_t exsid[104];             /* Extended SID buffer */
    uint32_t uid_low_masked;        /* Low UID masked for cache lookup */
    int8_t cache_result;            /* Cache lookup result */
    uint8_t clock_out[8];           /* Output clock from remote operation */

    uint16_t required_rights;
    int16_t option_flags;
    uint32_t rights_mask;
    int16_t rights_result;

    /* Extract flags components */
    required_rights = (uint16_t)(flags & 0xFFFF);
    option_flags = (int16_t)(flags >> 16);

    /* Copy UID and set up for lookup */
    lookup_context.uid_high = file_uid->high;
    lookup_context.uid_low = file_uid->low;

    /* Clear the remote flag before lookup */
    lookup_context.remote_flags &= ~0x40;  /* Clear bit 6 as in original */

    /* Get file location
     * AST_$GET_LOCATION signature: (uid_info, flags, unused, vol_uid_out, status)
     */
    AST_$GET_LOCATION(uid_info, 0, 0, &vol_uid_out, &location_status);

    if (location_status != status_$ok) {
        *status_ret = location_status;
        return;
    }

    /*
     * Check if file is remote (bit 7 of remote_flags)
     * In the decompiled code this corresponds to local_6f.
     */
    if ((lookup_context.remote_flags & 0x80) != 0) {
        /* Remote file handling */

        /* Get extended SID for access control */
        ACL_$GET_EXSID(exsid, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }

        /* Check hint cache to see if we can use local operations */
        uid_low_masked = lookup_context.uid_low & 0xFFFFF;
        HINT_$LOOKUP_CACHE(&uid_low_masked, &cache_result);

        if (cache_result >= 0) {
            /* Not in local cache - must use remote operation */
            REM_FILE_$FILE_SET_ATTRIB(
                lookup_context.location + 5,   /* Location info (auStack_7c + offset) */
                file_uid,                       /* File UID */
                value,                          /* Attribute value */
                attr_id,                        /* Attribute ID */
                exsid,                          /* Extended SID */
                required_rights,                /* Required rights */
                option_flags,                   /* Option flags */
                clock_out,                      /* Output clock */
                status_ret);

            /* Check if we got a rights error */
            if (*status_ret == status_$no_rights ||
                *status_ret == status_$file_bad_reply_received_from_remote ||
                *status_ret == status_$insufficient_rights) {
                /* Fall through to try local operation */
            } else if (*status_ret != status_$ok) {
                /* Some other error */
                return;
            } else {
                /* Success - update local AST cache */
                AST_$SET_ATTR(file_uid, attr_id, *(uint32_t *)value, 0,
                              (uint32_t *)clock_out, status_ret);
                return;
            }
        }
    }

    /* Check ACL if rights are required */
    if (required_rights != 0) {
        rights_mask = (uint32_t)required_rights;
        rights_result = ACL_$RIGHTS(file_uid, NULL, &rights_mask,
                                    &option_flags, status_ret);
        if (rights_result == 0) {
            /* Access denied - shut down wired pages */
            OS_PROC_SHUTWIRED(status_ret);
            return;
        }
    }

    /* Set the attribute locally via AST */
    AST_$SET_ATTRIBUTE(file_uid, attr_id, value, status_ret);
}
