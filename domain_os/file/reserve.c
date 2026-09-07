/*
 * FILE_$RESERVE - Reserve disk space for a file
 *
 * Original address: 0x00E74310
 * Size: 126 bytes
 *
 * Reserves disk space for a file without actually writing data.
 * This pre-allocates contiguous disk space to avoid fragmentation
 * and ensure space availability for future writes.
 *
 * Assembly analysis:
 *   - link.w A6,-0x14         ; Stack frame for local status and UID copy
 *   - Saves A5 to stack
 *   - Copies file_uid to local buffer
 *   - Dereferences start_byte and byte_count pointers
 *   - Calls ACL_$RIGHTS to check permission (rights_mask = 0x00000006)
 *   - If status_$ok, calls AST_$RESERVE
 *   - Otherwise calls OS_PROC_SHUTWIRED
 *   - Copies local status to status_ret
 *
 * Data constants (from PC-relative addressing; PC = instruction address + 2):
 *   0x00E7438E: word 0x0000       ACL_$RIGHTS option_flags  (pea (0x50,PC) at 0x00E7433C)
 *   0x00E74390: byte 0x00         ACL_$RIGHTS ignore_super  (pea (0x4a,PC) at 0x00E74344)
 *   0x00E74394: longword 0x06     ACL_$RIGHTS required_mask (pea (0x52,PC) at 0x00E74340)
 *
 * The word 0x2048 at 0x00E74392 sits between the last two cells and is NOT
 * one of ACL_$RIGHTS' arguments; an earlier reading of this function had it
 * as the rights mask.
 */

#include "file/file_internal.h"

/* Constant cells for the ACL_$RIGHTS call, pooled just past FILE_$RESERVE */

/* 0x00E74394: required rights mask (read 0x02 + write 0x04) */
static const uint32_t file_$reserve_rights_00e74394 = 0x00000006;

/* 0x00E74390: ACL_$RIGHTS' ignore_super argument, FALSE - the super-user
 * bypass applies. */
static const boolean file_$reserve_ignore_super_00e74390 = false;

/* 0x00E7438E: ACL_$RIGHTS' option-flags word */
static const int16_t file_$reserve_acl_opts_00e7438e = 0;

/*
 * FILE_$RESERVE - Reserve disk space for a file
 *
 * Pre-allocates disk space for a file. This can be used to ensure
 * that sufficient contiguous space is available before writing,
 * which can improve performance and prevent fragmentation.
 *
 * Parameters:
 *   file_uid   - UID of file to reserve space for
 *   start_byte - Pointer to starting byte offset
 *   byte_count - Pointer to number of bytes to reserve
 *   status_ret - Output status code
 *
 * Required rights:
 *   Read + write permission (0x06 mask) must be granted for the file.
 *
 * Status codes:
 *   status_$ok                  - Reservation succeeded
 *   status_$insufficient_rights - No write permission
 *   (other status from AST_$RESERVE)
 */
void FILE_$RESERVE(uid_t *file_uid, uint32_t *start_byte,
                   uint32_t *byte_count, status_$t *status_ret)
{
    uid_t local_uid;
    uint32_t start_val;
    uint32_t count_val;
    status_$t status;

    /* Copy UID to local buffer */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Dereference parameters */
    start_val = *start_byte;
    count_val = *byte_count;

    /*
     * Check permission using ACL_$RIGHTS.
     * Rights mask 0x06 = read (0x02) + write (0x04).
     */
    ACL_$RIGHTS(&local_uid,
                (boolean *)&file_$reserve_ignore_super_00e74390,
                (uint32_t *)&file_$reserve_rights_00e74394,
                (int16_t *)&file_$reserve_acl_opts_00e7438e,
                &status);

    if (status == status_$ok) {
        /*
         * Permission granted - reserve the space
         * AST_$RESERVE signature:
         *   AST_$RESERVE(uid, start_byte, byte_count, status)
         */
        AST_$RESERVE(&local_uid, start_val, count_val, &status);
    } else {
        /*
         * Permission denied - release wired pages
         */
        OS_PROC_SHUTWIRED(&status);
    }

    *status_ret = status;
}
