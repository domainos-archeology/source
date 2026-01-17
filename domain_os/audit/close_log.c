/*
 * close_log.c - audit_$close_log
 *
 * Closes the audit log file, flushing pending data and
 * truncating to the actual written size.
 *
 * Original address: 0x00E7185E
 */

#include "audit/audit_internal.h"
#include "file/file.h"
#include "mst/mst.h"

/* Forward declarations for functions not yet in headers */
extern void FILE_$TRUNCATE(uid_t *file_uid, int32_t *size, status_$t *status_ret);
extern void FILE_$PRIV_UNLOCK(uid_t *file_uid, int16_t lock_id,
                              uint32_t flags, uint32_t p4, uint32_t p5, uint32_t p6,
                              void *lock_info, status_$t *status_ret);

void audit_$close_log(status_$t *status_ret)
{
    int32_t final_size;
    uint8_t lock_info[12];

    /* Check if log file is open */
    if (AUDIT_$DATA.log_file_uid.high == UID_$NIL.high &&
        AUDIT_$DATA.log_file_uid.low == UID_$NIL.low) {
        /* Not open, nothing to do */
        *status_ret = status_$ok;
        return;
    }

    /* Clear dirty flag */
    AUDIT_$DATA.dirty = 0;

    /* Flush pending data */
    FILE_$FW_FILE(&AUDIT_$DATA.log_file_uid, status_ret);

    /* Unmap the buffer - cast pointer to uint32_t for m68k compat */
    MST_$UNMAP_PRIVI(1, &UID_$NIL,
                     (uint32_t)(uintptr_t)AUDIT_$DATA.buffer_base,
                     AUDIT_$DATA.buffer_size,
                     0, status_ret);

    /* Calculate final file size:
     * file_offset + (buffer_size - bytes_remaining) */
    final_size = AUDIT_$DATA.file_offset +
                 (AUDIT_$DATA.buffer_size - AUDIT_$DATA.bytes_remaining);

    /* Truncate file to actual size */
    FILE_$TRUNCATE(&AUDIT_$DATA.log_file_uid, &final_size, status_ret);

    /* Unlock the file */
    FILE_$PRIV_UNLOCK(&AUDIT_$DATA.log_file_uid, (int16_t)AUDIT_$DATA.lock_id,
                      0x40000, 0, 0, 0, lock_info, status_ret);

    /* Reset log file UID */
    AUDIT_$DATA.log_file_uid.high = UID_$NIL.high;
    AUDIT_$DATA.log_file_uid.low = UID_$NIL.low;
}
