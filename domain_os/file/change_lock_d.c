/*
 * FILE_$CHANGE_LOCK_D - Change an existing lock with domain context
 *
 * Original address: 0x00E5EA9E
 * Size: 130 bytes
 *
 * This is a wrapper function that calls FILE_$PRIV_LOCK with flags
 * for changing an existing lock mode. It first validates that the
 * requested lock mode is not in the illegal mode mask.
 *
 * Assembly analysis:
 *   - link.w A6,-0x4          ; 4 bytes local stack
 *   - Checks DAT_00e823f0 (FILE_$LOCK_ILLEGAL_MASK) against lock_mode
 *   - If illegal, returns file_$illegal_lock_request (0xF0007)
 *   - Otherwise calls FILE_$PRIV_LOCK with flags=0x440000 (change+upgrade)
 *   - If AUDIT_$ENABLED < 0, calls FILE_$AUDIT_LOCK
 */

#include "file/file_internal.h"

/*
 * 0x00E5E61E: the longword the compiler passes with `pea (-0x530,PC)` /
 * `pea (-0x436,PC)` / `pea (-0x4bc,PC)` as FILE_$PRIV_LOCK's ACL-context
 * argument.  It holds NIL.
 */
static void *file_$priv_lock_nil_acl_ctx = NULL;

/*
 * FILE_$CHANGE_LOCK_D - Change an existing lock's mode
 *
 * Parameters:
 *   file_uid     - UID of file with existing lock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to new lock mode
 *   slot_io      - In/out per-process lock slot (A6+0x14, already a pointer)
 *   status_ret   - Output status code
 */
void FILE_$CHANGE_LOCK_D(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                         uint32_t *slot_io, status_$t *status_ret)
{
    uint16_t result;
    uint16_t mode = *lock_mode;

    /*
     * Check if the requested lock mode is illegal.
     * FILE_$LOCK_ILLEGAL_MASK is a bitmask where bit N is set if mode N is illegal.
     * The original code: btst.l D0,D1 where D0 = mode, D1 = illegal_mask
     */
    if ((FILE_$LOCK_ILLEGAL_MASK & (1 << (mode & 0x1F))) != 0) {
        *status_ret = file_$illegal_lock_request;
    } else {
        /*
         * Call FILE_$PRIV_LOCK with:
         *   - PROC1_$AS_ID as the process ASID
         *   - *lock_index as the lock table index
         *   - mode as the lock mode
         *   - 0 as the rights byte (not used for change)
         *   - 0x440000 as flags (FILE_LOCK_FLAG_CHANGE | FILE_LOCK_FLAG_UPGRADE)
         *   - param_4 as the output context
         *   - Other parameters zeroed for local lock
         */
        FILE_$PRIV_LOCK(file_uid,
                        PROC1_$AS_ID,
                        *lock_index,      /* side */
                        mode,
                        0,                /* local_only = FALSE (0x00E5EAD6 clr.w) */
                        0x0044,           /* flags word (0x00E5EAE2 move.l #0x440000) */
                        0x0000,           /* key word */
                        0,                /* rem_key */
                        0,                /* rem_node */
                        0,                /* rem_extra */
                        &file_$priv_lock_nil_acl_ctx, /* 0x00E5EAD8 pea (-0x4bc,PC) */
                        0,                /* rem_wait */
                        slot_io,          /* 0x00E5EAD2 pushes the caller's pointer */
                        &result,          /* rights_out */
                        status_ret);
    }

    /*
     * If auditing is enabled (AUDIT_$ENABLED has high bit set),
     * log the lock change operation
     */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_LOCK(*status_ret, file_uid, mode);
    }
}
