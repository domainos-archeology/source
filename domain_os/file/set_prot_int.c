/*
 * FILE_$SET_PROT_INT - Set file protection (internal)
 *
 * Internal function for setting file protection. Handles both local
 * and remote files, ACL checking, and locksmith privileges.
 *
 * Original address: 0x00E5DD08
 */

#include "file/file_internal.h"

/* Status codes */
#define status_$file_objects_on_different_volumes   0x000F0013
#define status_$file_object_is_remote               0x000F0002
#define status_$file_bad_reply_received_from_remote 0x000F0003
#define status_$file_incompatible_request           0x000F0015
#define status_$ast_incompatible_request            0x00030006
#define status_$no_right_to_perform_operation       0x00230001
#define status_$acl_no_right_to_set_subsystem_data  0x00230010

/* PROC1 type for server process */
#define PROC1_TYPE_SERVER                           9

/* External AUDIT_$ENABLED flag */
extern int8_t AUDIT_$ENABLED;

/*
 * FILE_$SET_PROT_INT
 *
 * Core internal function for setting file protection.
 *
 * Parameters:
 *   file_uid     - UID of file to modify
 *   acl_data     - ACL data buffer (44 bytes)
 *   attr_type    - Protection attribute type
 *   prot_type    - Protection type being set
 *   subsys_flag  - Subsystem data flag (negative to allow override)
 *   status_ret   - Output status code
 *
 * Flow:
 * 1. If ACL source UID is present, check if on same volume
 * 2. If file is remote, forward to REM_FILE_$FILE_SET_PROT
 * 3. If local, check permissions via ACL_$SET_ACL_CHECK
 * 4. Apply locksmith overrides if appropriate
 * 5. Set attribute via AST_$SET_ATTRIBUTE
 * 6. Log audit event if auditing is enabled
 */
void FILE_$SET_PROT_INT(uid_t *file_uid, void *acl_data, uint16_t attr_type,
                        uint16_t prot_type, int16_t subsys_flag,
                        status_$t *status_ret)
{
    int8_t same_volume_result = 0;
    status_$t local_status;

    /* Location info for remote handling */
    struct {
        uint32_t data[8];       /* 32 bytes */
    } location_info;

    /*
     * Lookup context structure for AST_$GET_LOCATION
     * AST_$GET_LOCATION expects UID at offset 8 within the structure.
     * It writes location data and sets remote_flags bit 7 if remote.
     */
    struct {
        uint32_t data[2];       /* 8 bytes - location output starts here */
        uid_t    uid;           /* 8 bytes - UID input at offset 8 */
        uint32_t data2[4];      /* 16 bytes - more location output */
        uint8_t  pad[5];
        uint8_t  remote_flags;  /* Bit 7 set if remote */
    } lookup_context;

    uint32_t vol_uid_out;       /* Volume UID output from AST_$GET_LOCATION */

    /* For ACL operations */
    uint8_t exsid[104];         /* Extended SID buffer */
    clock_t attr_result;        /* Attribute result from remote op (modification time) */
    int8_t acl_check_result;
    int8_t permission_flags[2];
    int16_t locksmith_result;

    /* Check if ACL source UID is present (at offset 0x2C from acl_data base) */
    uint8_t *acl_source_uid = (uint8_t *)acl_data + 0x2C;

    if (*acl_source_uid != 0) {
        /*
         * ACL source UID present - check if on same volume.
         * If not same volume, we need to handle specially.
         */
        same_volume_result = FILE_$CHECK_SAME_VOLUME(file_uid,
                                                      (uid_t *)acl_source_uid,
                                                      -1,  /* Copy location */
                                                      location_info.data,
                                                      status_ret);

        if (same_volume_result >= 0) {
            /* Check returned status */
            if (*status_ret == status_$ok) {
                *status_ret = status_$file_objects_on_different_volumes;
                goto audit_and_return;
            }
            if (*status_ret != status_$file_object_is_remote) {
                goto audit_and_return;
            }
            same_volume_result = -1;  /* Mark as remote */
        }
    }

    if (same_volume_result >= 0) {
        /*
         * Local file - get location info
         * Copy UID to lookup_context.uid field (at offset 8)
         */
        lookup_context.uid.high = file_uid->high;
        lookup_context.uid.low = file_uid->low;
        lookup_context.remote_flags &= ~0x40;

        AST_$GET_LOCATION((uint32_t *)&lookup_context, 0, 0, &vol_uid_out, &local_status);

        if (local_status != status_$ok) {
            *status_ret = local_status;
            goto audit_and_return;
        }
    }

    /*
     * Check if file is remote (bit 7 of remote_flags)
     */
    if ((int8_t)lookup_context.remote_flags < 0) {
        /* Remote file handling */

        /* Get extended SID */
        ACL_$GET_EXSID(exsid, status_ret);
        if (*status_ret != status_$ok) {
            goto audit_and_return;
        }

        /* Call remote file set protection */
        REM_FILE_$FILE_SET_PROT(&lookup_context,
                                file_uid,
                                acl_data,
                                attr_type,
                                exsid,
                                (uint8_t)subsys_flag,
                                &attr_result,
                                status_ret);

        if (*status_ret == status_$file_bad_reply_received_from_remote) {
            /* Fall through to local operation */
        } else if (*status_ret == status_$ok) {
            /* Update local AST cache with result */
            AST_$SET_ATTR(file_uid, attr_type, *(uint32_t *)acl_data, 0,
                          (uint32_t *)&attr_result, status_ret);
            goto audit_and_return;
        } else {
            goto audit_and_return;
        }
    }

    /*
     * Local file - check ACL permissions
     */
    acl_check_result = ACL_$SET_ACL_CHECK(file_uid, acl_data,
                                           (uid_t *)acl_source_uid,
                                           (int16_t *)&prot_type,
                                           permission_flags,
                                           status_ret);

    if (acl_check_result >= 0) {
        /*
         * Check if permission flags indicate no rights, and we're in server process.
         * Locksmith can override this.
         */
        if ((int8_t)permission_flags[0] < 0) {
            /* Check if current process is a server (type 9) */
            if (PROC1_$TYPE[(int16_t)PROC1_$CURRENT] == PROC1_TYPE_SERVER) {
                locksmith_result = ACL_$GET_LOCAL_LOCKSMITH();
                if (locksmith_result != 0) {
                    /* Not a locksmith - deny access */
                    *status_ret = status_$no_right_to_perform_operation;
                    goto audit_and_return;
                }
            }
        }

        /*
         * Check if subsystem data permission was denied but we can override.
         */
        if (subsys_flag < 0 &&
            *status_ret == status_$acl_no_right_to_set_subsystem_data) {
            locksmith_result = ACL_$GET_LOCAL_LOCKSMITH();
            if (locksmith_result == 0 ||
                PROC1_$TYPE[(int16_t)PROC1_$CURRENT] == PROC1_TYPE_SERVER) {
                /* Can override - clear error */
                *status_ret = status_$ok;
            }
        }

        if (*status_ret != status_$ok) {
            /* Permission denied - shutdown wired pages */
            OS_PROC_SHUTWIRED(status_ret);
            goto audit_and_return;
        }
    }

    /*
     * Set the attribute via AST
     */
    AST_$SET_ATTRIBUTE(file_uid, attr_type, acl_data, status_ret);

    /* Map AST incompatible request to FILE incompatible request */
    if (*status_ret == status_$ast_incompatible_request) {
        *status_ret = status_$file_incompatible_request;
    }

audit_and_return:
    /* Log audit event if auditing is enabled */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_SET_PROT(file_uid, acl_data,
                             (uint8_t *)acl_data + 0x2C,  /* ACL source UID */
                             prot_type, *status_ret);
    }
}
