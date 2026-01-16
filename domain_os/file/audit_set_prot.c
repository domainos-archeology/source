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
    uint32_t event_id[2];
    uint16_t status_flags[3];
    uint32_t audit_data[11];
    struct {
        uint32_t file_uid_high;
        uint32_t file_uid_low;
        uint32_t prot_high;
        uint32_t prot_low;
        uint16_t prot_type;
    } event_info;
    int16_t i;
    uint32_t *src;
    uint32_t *dst;

    /* Set up event ID */
    event_id[0] = FILE_AUDIT_EVENT_SET_PROT;
    event_id[1] = 0;

    /* Copy ACL data (11 uint32_t values = 44 bytes) */
    src = (uint32_t *)acl_data;
    dst = audit_data;
    for (i = 10; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* Copy file UID */
    event_info.file_uid_high = file_uid->high;
    event_info.file_uid_low = file_uid->low;

    /* Copy protection info (8 bytes) */
    event_info.prot_high = ((uint32_t *)prot_info)[0];
    event_info.prot_low = ((uint32_t *)prot_info)[1];

    /* Store protection type */
    event_info.prot_type = prot_type;

    /* Set status flags - non-zero if operation succeeded */
    status_flags[0] = (status != status_$ok) ? 1 : 0;

    /* Log the audit event */
    AUDIT_$LOG_EVENT(event_id, status_flags, &status, audit_data, &event_info);
}
