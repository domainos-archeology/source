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

/* AUDIT_$LOG_EVENT - Core audit event logging
 * Note: The canonical declaration is in audit/audit.h with typed parameters
 * (uid_t*, uint16_t*, uint32_t*, char*, uint16_t*). This function is called
 * here with pointer-to-local-stack variables that match those types. */
#include "audit/audit.h"
#include "arch/arch.h"

/* 0x00E4BF9E: the naming-resolve audit event code. */
#define AUDIT_EVENT_NAMING_RESOLVE  0x00040021

void audit_$log_resolve_op(uint32_t pname_data, uint16_t path_len,
                           void *result, status_$t status)
{
    /*
     * A6-0x10: the event UID.  The image writes it as two longwords
     * (0x00E4BF9E "move.l #0x40021,(-0x10,A6)" and 0x00E4BFA6
     * "clr.l (-0xc,A6)") and hands its address over as one 8-byte record.
     */
    uid_t event_uid;
    audit_$resolve_data_t data;         /* A6-0x418 */
    uint16_t data_len;                  /* A6-0x422 */
    uint16_t flags;                     /* A6-0x424 */
    uint32_t len;                       /* D2 */

    event_uid.high = AUDIT_EVENT_NAMING_RESOLVE;
    event_uid.low = 0;

    /*
     * 0x00E4BFAA-0x00E4BFB0: "clr.w D2w / cmp.w D0w,D2w / bhi" - an unsigned
     * "is 0 greater than path_len" test, which never holds, so the length is
     * always the caller's.  Reproduced as found.
     */
    len = (uint32_t)path_len;

    /* 0x00E4BFB4-0x00E4BFC0: two longword moves out of the resolve result */
    data.result_uid = *(const uid_t *)result;

    /* 0x00E4BFC4-0x00E4BFD8 */
    if (len != 0) {
        OS_$DATA_COPY(ARCH_VA_TO_PTR(pname_data), data.path, len);
    }

    /* 0x00E4BFDC-0x00E4BFE0 */
    data.path[len] = '\0';

    /*
     * 0x00E4BFE4-0x00E4BFF2: the length is the distance from the record's
     * base to the NUL, inclusive - path_len + 9 - stored as a WORD.
     */
    data_len = (uint16_t)((uint32_t)(&data.path[len] - (char *)&data) + 1);

    /* 0x00E4BFF6-0x00E4C006 */
    flags = (status != 0) ? 1 : 0;

    /*
     * 0x00E4C008-0x00E4C01C.  The status handed over is the ADDRESS of this
     * routine's own status argument slot ("pea (0x12,A6)"), and the data
     * pointer is the record base.
     */
    AUDIT_$LOG_EVENT(&event_uid, &flags, &status, (char *)&data, &data_len);
}
