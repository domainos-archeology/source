/*
 * FILE_$SET_PROT_INT - Set file protection (internal)
 *
 * Internal function for setting file protection. Handles both local
 * and remote files, ACL checking, and locksmith privileges.
 *
 * Original address: 0x00E5DD08
 */

#include "file/file_internal.h"
#include "acl/acl.h"

/* Status codes */
#define file_$objects_on_different_volumes   0x000F0013
#define file_$object_is_remote               0x000F0002
#define file_$bad_reply_received_from_remote 0x000F0003
#define file_$incompatible_request           0x000F0015
/* status_$no_right_to_perform_operation comes from acl/acl.h. */

/* PROC1 type for server process */
#define PROC1_TYPE_SERVER                           9

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
 *   subsys_flag  - Subsystem data flag, a Domain BOOLEAN byte (negative =
 *                  true = allow the locksmith override below).
 *
 *                  0x00E5DD1E reads it with `move.b (0x14,A6),D3b`, the HIGH
 *                  (even) half of the word slot at A6+0x14 - the half a byte
 *                  push lands in - and 0x00E5DE6A tests it `tst.b` / `bpl`.
 *                  It is re-pushed to REM_FILE_$FILE_SET_PROT as a byte
 *                  (`move.b D3b,-(SP)` at 0x00E5DDC6), and the REM_FILE_
 *                  server pushes it as a byte too (`move.b (-0x42c,A2),-(SP)`
 *                  at 0x00E634BA), so it is one byte end to end - the tree
 *                  used to declare it int16_t and hand the callee the LOW
 *                  byte.  (source-w7lk)
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
                        uint16_t prot_type, boolean subsys_flag,
                        status_$t *status_ret)
{
    int8_t same_volume_result = 0;
    status_$t local_status;

    /*
     * A6-0x20: the single 0x20-byte object-location record.  It is both
     * FILE_$CHECK_SAME_VOLUME's `location_out` buffer (`pea (-0x20,A6)` at
     * 0x00E5DD36) and AST_$GET_LOCATION's record (`pea (-0x20,A6)` at
     * 0x00E5DD8C) - the two uses share one cell in the original.
     */
    file_$obj_loc_t lookup_context;

    uint32_t loc_unused;        /* A6-0x98: pea'd, never touched */
    uint32_t vol_uid_out;       /* A6-0x9C: receives aote+0x08 */

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
                                                      (uint32_t *)(void *)
                                                          &lookup_context,
                                                      status_ret);

        if (same_volume_result >= 0) {
            /* Check returned status */
            if (*status_ret == status_$ok) {
                *status_ret = file_$objects_on_different_volumes;
                goto audit_and_return;
            }
            if (*status_ret != file_$object_is_remote) {
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
        lookup_context.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

        AST_$GET_LOCATION(&lookup_context, 0, &loc_unused, &vol_uid_out,
                          &local_status);

        if (local_status != status_$ok) {
            *status_ret = local_status;
            goto audit_and_return;
        }
    }

    /*
     * Check if file is remote (bit 7 of remote_flags)
     */
    if (lookup_context.flags < 0) {
        /* Remote file handling */

        /* Get extended SID */
        ACL_$GET_EXSID(exsid, status_ret);
        if (*status_ret != status_$ok) {
            goto audit_and_return;
        }

        /* Call remote file set protection */
        /* 0x00E5DDD4: `pea (-0x10,A6)` = the record base + 0x10. */
        REM_FILE_$FILE_SET_PROT(&lookup_context.loc_info,
                                file_uid,
                                acl_data,
                                attr_type,
                                exsid,
                                /* 0x00E5DDC6 `move.b D3b,-(SP)` */
                                (uint8_t)(subsys_flag & 0xFF),
                                &attr_result,
                                status_ret);

        if (*status_ret == file_$bad_reply_received_from_remote) {
            /* Fall through to local operation */
        } else if (*status_ret == status_$ok) {
            /* Update local AST cache with result */
            /* 0x00E5DDFA pea's the acl_data POINTER, not its contents. */
            AST_$SET_ATTR(file_uid, attr_type, acl_data, 0,
                          &attr_result, status_ret);
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
            if (PROC1_$DATA.type[(int16_t)PROC1_$CURRENT] == PROC1_TYPE_SERVER) {
                locksmith_result = ACL_$GET_LOCAL_LOCKSMITH();
                if (locksmith_result != 0) {
                    /* Not a locksmith - deny access */
                    *status_ret = status_$no_right_to_perform_operation;
                    goto audit_and_return;
                }
            }
        }

        /*
         * 0x00E5DE6A-0x00E5DE9A: the caller asked to set subsystem data and
         * was refused; decide whether to forgive the refusal.
         *
         *   00e5de6a  tst.b  D3b
         *   00e5de6c  bpl.b  0x00e5de9c        ; not asked for -> leave it
         *   00e5de6e  cmpi.l #0x230010,(A4)
         *   00e5de74  bne.b  0x00e5de9c        ; a different error -> leave it
         *   00e5de76  jsr    ACL_$GET_LOCAL_LOCKSMITH
         *   00e5de7c  tst.w  D0w
         *   00e5de7e  seq    D2b               ; TRUE when the call returned 0
         *   00e5de80  tst.b  D2b
         *   00e5de82  bmi.b  0x00e5de9a        ; returned 0 -> CLEAR
         *   00e5de84  move.w PROC1_$CURRENT,D2w
         *   00e5de92  cmpi.w #0x9,PROC1_$DATA.type[D2]
         *   00e5de98  beq.b  0x00e5de9c        ; type IS 9 -> leave it
         *   00e5de9a  clr.l  (A4)              ; CLEAR
         *
         * So the status is cleared when the call returned 0 OR when the
         * current process is NOT type 9.  The tree had the second disjunct
         * inverted (it cleared when the type WAS 9).  (source-w7lk)
         *
         * Note the opposite sense of the two ACL_$GET_LOCAL_LOCKSMITH tests
         * in this routine: 0x00E5DE5C uses `sne` (deny when non-zero) and
         * 0x00E5DE7E uses `seq` (forgive when zero) - both mean "zero is the
         * locksmith".
         */
        if (subsys_flag < 0 &&
            *status_ret == status_$acl_no_right_to_set_subsystem_data) {
            boolean is_locksmith;       /* D2, from `seq D2b` */

            locksmith_result = ACL_$GET_LOCAL_LOCKSMITH();
            is_locksmith = (locksmith_result == 0) ? -1 : 0;

            if (is_locksmith < 0) {
                *status_ret = status_$ok;               /* 0x00E5DE9A */
            } else if (PROC1_$DATA.type[(int16_t)PROC1_$CURRENT]
                           != PROC1_TYPE_SERVER) {
                *status_ret = status_$ok;               /* 0x00E5DE9A */
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
        *status_ret = file_$incompatible_request;
    }

audit_and_return:
    /* Log audit event if auditing is enabled */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_SET_PROT(file_uid, acl_data,
                             (uint8_t *)acl_data + 0x2C,  /* ACL source UID */
                             prot_type, *status_ret);
    }
}
