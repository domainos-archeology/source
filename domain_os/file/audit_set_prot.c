/*
 * FILE_$AUDIT_SET_PROT - Log audit event for protection changes
 *
 * Logs a protection/ACL change audit event if auditing is enabled.
 *
 * Original address: 0x00E5DC96
 */

#include "file/file_internal.h"

/*
 * Audit event type for set protection operations
 * Format: 0x4000A - FILE subsystem audit event ID
 */
#define FILE_AUDIT_EVENT_SET_PROT   0x0004000A

/*
 * FILE_$AUDIT_SET_PROT
 *
 * Logs an audit event when file protection is modified.
 * Called from FILE_$SET_PROT_INT and FILE_$SET_ACL.
 *
 * Parameters:
 *   file_uid   - UID of file being modified
 *   acl_data   - ACL data buffer (44 bytes, contains owner/group/org UIDs and rights)
 *   prot_info  - Protection info (8 bytes)
 *   prot_type  - Protection type being set
 *   status     - Status of the operation
 *
 * The function constructs an audit log entry containing:
 *   - Event ID (0x4000A)
 *   - File UID
 *   - ACL data
 *   - Protection info
 *   - Protection type
 *   - Success/failure indicator
 */
void FILE_$AUDIT_SET_PROT(uid_t *file_uid, void *acl_data, void *prot_info,
                          uint16_t prot_type, status_$t status)
{
    uid_t event_uid;
    uint16_t event_flags;
    uint32_t status_code;
    char audit_data[62];  /* 44 bytes ACL + 8 bytes file_uid + 8 bytes prot_info + 2 bytes prot_type */
    uint16_t data_len;
    int16_t i;
    uint32_t *src;
    char *dst;

    /* Set up event UID - FILE subsystem audit event for set protection */
    event_uid.high = FILE_AUDIT_EVENT_SET_PROT;
    event_uid.low = 0;

    /* Copy ACL data (44 bytes) */
    src = (uint32_t *)acl_data;
    dst = audit_data;
    for (i = 0; i < 11; i++) {
        *(uint32_t *)dst = src[i];
        dst += 4;
    }

    /* Copy file UID (8 bytes) */
    *(uint32_t *)dst = file_uid->high;
    dst += 4;
    *(uint32_t *)dst = file_uid->low;
    dst += 4;

    /* Copy protection info (8 bytes) */
    *(uint32_t *)dst = ((uint32_t *)prot_info)[0];
    dst += 4;
    *(uint32_t *)dst = ((uint32_t *)prot_info)[1];
    dst += 4;

    /* Store protection type (2 bytes) */
    *(uint16_t *)dst = prot_type;
    dst += 2;

    /* Set event flags - non-zero if operation failed */
    event_flags = (status != status_$ok) ? 1 : 0;

    /* Convert status to uint32_t */
    status_code = (uint32_t)status;

    /* Calculate data length */
    data_len = (uint16_t)(dst - audit_data);

    /* Log the audit event */
    AUDIT_$LOG_EVENT(&event_uid, &event_flags, &status_code, audit_data, &data_len);
}
