/*
 * open_log.c - audit_$open_log
 *
 * Opens or creates the audit log file and maps a buffer for writing.
 *
 * Original address: 0x00E716CC
 */

#include "audit/audit_internal.h"
#include "name/name.h"
#include "file/file_internal.h" // we use FILE_$PRIV_LOCK/UNLOCK
#include "mst/mst.h"

/* Path to audit log file */
static const char log_path[] = "//node_data/audit/audit_log";
static int16_t log_path_len = 26;

/* File type UID for unstructured files */
extern uid_t UNSTRUCT_$UID;

void audit_$open_log(status_$t *status_ret)
{
    uint8_t attr_buffer[40];
    uint32_t file_size;
    uint8_t lock_info[10];

    /* Check if log file is already open */
    if (AUDIT_$DATA.log_file_uid.high != UID_$NIL.high ||
        AUDIT_$DATA.log_file_uid.low != UID_$NIL.low) {
        /* Already open */
        *status_ret = status_$ok;
        return;
    }

    /* Try to resolve the log file path */
    NAME_$RESOLVE((char *)log_path, &log_path_len, &AUDIT_$DATA.log_file_uid, status_ret);

    if (*status_ret == status_$naming_name_not_found) {
        /* File doesn't exist, create it */
        NAME_$CR_FILE((char *)log_path, &log_path_len, &AUDIT_$DATA.log_file_uid, status_ret);
    }

    if (*status_ret != status_$ok) {
        goto error;
    }

    /* Set file type to unstructured */
    FILE_$SET_TYPE(&AUDIT_$DATA.log_file_uid, &UNSTRUCT_$UID, status_ret);
    if (*status_ret != status_$ok) {
        goto error;
    }

    /* Get file attributes to determine current size */
    FILE_$GET_ATTRIBUTES(&AUDIT_$DATA.log_file_uid, NULL, NULL,
                         NULL, attr_buffer, status_ret);
    if (*status_ret != status_$ok) {
        goto error;
    }

    /* Extract file size from attributes (at offset 0x1C in attr_buffer) */
    file_size = *(uint32_t *)(attr_buffer + 0x1C);
    AUDIT_$DATA.file_offset = file_size;

    /* Lock the file for exclusive access */
    FILE_$PRIV_LOCK(&AUDIT_$DATA.log_file_uid, 0, 1, 4, 0, 0, 0, 0, 0,
                    NULL, 0, &AUDIT_$DATA.lock_id, lock_info, status_ret);
    if (*status_ret != status_$ok) {
        goto error;
    }

    /* Map a buffer for writing
     * Note: MST_$MAPS_RET is an alias that returns the mapped address */
    AUDIT_$DATA.buffer_base = MST_$MAPS_RET(
        0,                          /* asid (0 = current) */
        (int8_t)-1,                 /* flags */
        &AUDIT_$DATA.log_file_uid,
        AUDIT_$DATA.file_offset,
        AUDIT_BUFFER_MAP_SIZE,
        0x16,                       /* protection */
        0,                          /* unused */
        (int8_t)-1,                 /* writable */
        &AUDIT_$DATA.buffer_size,
        status_ret
    );

    if (*status_ret != status_$ok) {
        /* Mapping failed, unlock and cleanup */
        FILE_$PRIV_UNLOCK(&AUDIT_$DATA.log_file_uid, (int16_t)AUDIT_$DATA.lock_id,
                          0x40000, 0, 0, 0, lock_info, status_ret);
        goto error;
    }

    /* Initialize write pointers */
    AUDIT_$DATA.write_ptr = AUDIT_$DATA.buffer_base;
    AUDIT_$DATA.bytes_remaining = AUDIT_$DATA.buffer_size;
    AUDIT_$DATA.dirty = 0;

    return;

error:
    /* Reset log file state on error */
    AUDIT_$DATA.log_file_uid.high = UID_$NIL.high;
    AUDIT_$DATA.log_file_uid.low = UID_$NIL.low;
    AUDIT_$DATA.write_ptr = NULL;
    AUDIT_$DATA.file_offset = 0;
    AUDIT_$DATA.bytes_remaining = 0;
    AUDIT_$DATA.dirty = 0;
}
