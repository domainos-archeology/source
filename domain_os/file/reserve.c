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
 *   - Calls ACL_$RIGHTS to check write permission (rights_mask = 0x2048)
 *   - If status_$ok, calls AST_$RESERVE
 *   - Otherwise calls OS_PROC_SHUTWIRED
 *   - Copies local status to status_ret
 *
 * Data constants (from PC-relative addressing):
 *   DAT_00e7438e: 0x0000       (option flags = 0)
 *   DAT_00e74390: 0x00002048   (required rights mask)
 *   DAT_00e74394: 0x00000006   (unused parameter)
 */

#include "file/file_internal.h"

/* Constants for ACL_$RIGHTS call */
static const uint32_t reserve_rights_mask = 0x00002048;  /* Write + extend rights */
static const int16_t reserve_option_flags = 0;

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
 *   Write permission (0x2048 mask) must be granted for the file.
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
     * Check write/extend permission using ACL_$RIGHTS
     * Rights mask 0x2048 = write + extend (bit 6 + bit 11)
     */
    ACL_$RIGHTS(&local_uid, (void *)&reserve_rights_mask,
                (uint32_t *)&reserve_rights_mask, (int16_t *)&reserve_option_flags,
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
