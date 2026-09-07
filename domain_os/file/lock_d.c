/*
 * FILE_$LOCK_D - Lock a file with domain context
 *
 * Original address: 0x00E5EA2A
 * Size: 116 bytes
 *
 * This is a wrapper function that calls FILE_$PRIV_LOCK with specific
 * flags for domain (distributed) locking operations.
 *
 * Assembly analysis:
 *   - link.w A6,-0x4          ; 4 bytes local stack
 *   - Calls FILE_$PRIV_LOCK at 0x00E5F0EE with flags=0x40000 (upgrade)
 *   - If AUDIT_$ENABLED < 0, calls FILE_$AUDIT_LOCK at 0x00E5E88A
 */

#include "file/file_internal.h"

/*
 * 0x00E5E61E: the longword the compiler passes with `pea (-0x530,PC)` /
 * `pea (-0x436,PC)` / `pea (-0x4bc,PC)` as FILE_$PRIV_LOCK's ACL-context
 * argument.  It holds NIL.
 */
static void *file_$priv_lock_nil_acl_ctx = NULL;

/*
 * FILE_$LOCK_D - Lock a file with domain context
 *
 * Parameters:
 *   file_uid     - UID of file to lock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to lock mode
 *   rights       - Pointer to rights byte
 *   slot_io      - In/out per-process lock slot (A6+0x18, pushed by value
 *                  at 0x00E5EA4C: it is already a pointer)
 *   status_ret   - Output status code
 */
void FILE_$LOCK_D(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                  uint8_t *rights, uint32_t *slot_io, status_$t *status_ret)
{
    uint16_t result;

    /*
     * Call FILE_$PRIV_LOCK with:
     *   - PROC1_$AS_ID as the process ASID
     *   - *lock_index as the lock table index
     *   - *lock_mode as the lock mode
     *   - *rights as the rights byte (extended to 16-bit with upper byte from stack)
     *   - 0x40000 as flags (FILE_LOCK_FLAG_UPGRADE)
     *   - param_5 as the output context
     *   - Other parameters zeroed for local lock
     */
    FILE_$PRIV_LOCK(file_uid,
                    PROC1_$AS_ID,
                    *lock_index,          /* side */
                    *lock_mode,
                    (boolean)*rights,     /* local_only (byte at A6+0x12) */
                    0x0004,               /* flags word (0x00E5EA5C move.l #0x40000) */
                    0x0000,               /* key word */
                    0,                    /* rem_key */
                    0,                    /* rem_node */
                    0,                    /* rem_extra */
                    &file_$priv_lock_nil_acl_ctx,   /* 0x00E5EA52 pea (-0x436,PC) */
                    0,                    /* rem_wait */
                    slot_io,              /* 0x00E5EA4C pushes the caller's pointer */
                    &result,              /* rights_out */
                    status_ret);

    /*
     * If auditing is enabled (AUDIT_$ENABLED has high bit set),
     * log the lock operation
     */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        FILE_$AUDIT_LOCK(*status_ret, file_uid, *lock_mode);
    }
}
