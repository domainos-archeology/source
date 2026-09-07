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
#define file_$bad_reply_received_from_remote    0x000F0003
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
 *   rights     - Required-rights mask (word at A6+0x12); 0 skips the check
 *   options    - ACL option flags (word at A6+0x14)
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
/*
 * 0x00E5D380: byte 0, ACL_$RIGHTS' `ignore_super` argument
 * (`pea (0x36,PC)` at 0x00E5D348).  FALSE, so the super-user bypass applies.
 */
static const boolean file_$set_attr_ignore_super = false;

void FILE_$SET_ATTRIBUTE(uid_t *file_uid, int16_t attr_id, void *value,
                         uint16_t rights, int16_t options,
                         status_$t *status_ret)
{
    status_$t location_status;

    /*
     * A6-0x88: the 0x20-byte object-location record.  The caller's UID goes
     * in at +0x08 (A6-0x80, 0x00E5D25C) and the flags byte lives at +0x1D
     * (A6-0x6B, `bclr.b #6,(-0x6b,A6)` at 0x00E5D264).
     */
    file_$obj_loc_t lookup_context;

    uint32_t loc_unused;            /* A6-0x9C: pea'd, never touched */
    uint32_t vol_uid_out;           /* A6-0x98: receives aote+0x08 */

    /* For remote files */
    uint8_t exsid[104];             /* A6-0x68: extended SID buffer */
    uint32_t uid_low_masked;        /* A6-0xA4: low UID masked for the cache */
    int8_t cache_result;            /* A6-0xA0: cache lookup result */
    clock_t mtime_out;              /* A6-0x90: mod time from the remote op */

    uint16_t required_rights = rights;   /* word at A6+0x12 */
    int16_t option_flags = options;      /* word at A6+0x14 */
    uint32_t rights_mask;
    int16_t rights_result;

    /* Copy UID into the record at +0x08 and set up for lookup */
    lookup_context.uid = *file_uid;

    /* Clear bit 6 of the record's flags byte before the lookup */
    lookup_context.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    AST_$GET_LOCATION(&lookup_context, 0, &loc_unused, &vol_uid_out,
                      &location_status);

    if (location_status != status_$ok) {
        *status_ret = location_status;
        return;
    }

    /*
     * Check if file is remote (bit 7 of remote_flags)
     * In the decompiled code this corresponds to local_6f.
     */
    if (lookup_context.flags < 0) {
        /* Remote file handling */

        /* Get extended SID for access control */
        ACL_$GET_EXSID(exsid, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }

        /* Check hint cache to see if we can use local operations */
        uid_low_masked = lookup_context.uid.low & 0xFFFFF;
        HINT_$LOOKUP_CACHE(&uid_low_masked, &cache_result);

        if (cache_result >= 0) {
            /* Not in local cache - must use remote operation */
            REM_FILE_$FILE_SET_ATTRIB(
                &lookup_context.loc_info,       /* 0x00E5D2EE: record + 0x10 */
                file_uid,                       /* File UID */
                value,                          /* Attribute value */
                attr_id,                        /* Attribute ID */
                exsid,                          /* Extended SID */
                required_rights,                /* Required rights */
                option_flags,                   /* Option flags */
                &mtime_out,                     /* Output modification time */
                status_ret);

            /* Check if we got a rights error */
            if (*status_ret == status_$no_rights ||
                *status_ret == file_$bad_reply_received_from_remote ||
                *status_ret == status_$insufficient_rights) {
                /* Fall through to try local operation */
            } else if (*status_ret != status_$ok) {
                /* Some other error */
                return;
            } else {
                /* Success - update local AST cache */
                /* 0x00E5D322 pea's the value POINTER, not its contents. */
                AST_$SET_ATTR(file_uid, attr_id, value, 0,
                              &mtime_out, status_ret);
                return;
            }
        }
    }

    /* Check ACL if rights are required */
    if (required_rights != 0) {
        rights_mask = (uint32_t)required_rights;
        /* 0x00E5D358 `tst.w D0w`: only the low word of the longword result
         * is examined, so rights_result stays 16 bits wide. */
        rights_result = (int16_t)ACL_$RIGHTS(file_uid,
                                             (boolean *)&file_$set_attr_ignore_super,
                                             &rights_mask, &option_flags,
                                             status_ret);
        if (rights_result == 0) {
            /* Access denied - shut down wired pages */
            OS_PROC_SHUTWIRED(status_ret);
            return;
        }
    }

    /* Set the attribute locally via AST */
    AST_$SET_ATTRIBUTE(file_uid, attr_id, value, status_ret);
}
