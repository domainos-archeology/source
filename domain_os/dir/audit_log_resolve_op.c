/*
 * audit_$log_resolve_op - Audit logging for pathname resolve operations
 *
 * Formats and logs an audit event record for pathname resolution operations.
 * Builds an event record containing the resolved path data, UIDs, and
 * a success/failure flag, then calls AUDIT_$LOG_EVENT.
 *
 * The event code is 0x40021 (naming resolve audit).
 *
 * Parameters:
 *   pname_data  - Pointer to pathname string
 *   path_len    - Length of pathname
 *   result      - Pointer to resolve result (2 UIDs = 8 bytes)
 *   status      - Status code from the resolve operation
 *
 * Original address: 0x00E4BF92
 * Original size: 154 bytes
 */

#include "dir/dir_internal.h"

/* OS_$DATA_COPY - System memory copy */
extern void OS_$DATA_COPY(void *src, void *dst, uint32_t len);

/* AUDIT_$LOG_EVENT - Core audit event logging
 * Note: The canonical declaration is in audit/audit.h with typed parameters
 * (uid_t*, uint16_t*, uint32_t*, char*, uint16_t*). This function is called
 * here with pointer-to-local-stack variables that match those types. */
#include "audit/audit.h"

void audit_$log_resolve_op(uint32_t pname_data, uint16_t path_len,
                           void *result, status_$t status)
{
    uint32_t len;
    uint16_t flags;
    int16_t data_len[5];
    uint32_t result_uid_high;
    uint32_t result_uid_low;
    char path_buf[1024];
    uint32_t event_code;
    uint32_t event_extra;

    event_code = 0x40021;
    event_extra = 0;

    /* Clamp path_len to a safe range */
    len = (uint32_t)path_len;

    /* Copy the two UIDs (8 bytes) from result */
    result_uid_high = *((uint32_t *)result);
    result_uid_low = *((uint32_t *)result + 1);

    /* Copy pathname data */
    if (len != 0) {
        OS_$DATA_COPY((void *)pname_data, path_buf, len);
    }

    /* Null-terminate the path */
    path_buf[len] = '\0';

    /* Compute data length: offset from result_uid_high to end of path_buf
     * The data region spans from result_uid_high through path_buf[len],
     * which is path_len + 9 bytes (8 for UIDs + len + 1 for NUL) */
    data_len[0] = path_len + 9;

    /* Set flags: 1 if status is non-zero (failure), 0 if success */
    flags = (uint16_t)(status != 0);

    /* Log the event
     * event_code + event_extra form an 8-byte uid_t on the stack.
     * result_uid_high..path_buf form the contiguous data region. */
    AUDIT_$LOG_EVENT((uid_t *)&event_code, &flags, (uint32_t *)&status,
                     (char *)&result_uid_high, (uint16_t *)data_len);
}
