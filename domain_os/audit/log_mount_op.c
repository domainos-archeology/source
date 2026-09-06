/*
 * audit_$log_mount_op - Log mount operation audit event
 *
 * Logs an audit event for mount/drop-mount directory operations.
 * Constructs an audit record containing:
 *   - Event type (4) and audit subtype from parameter
 *   - Directory UID (8 bytes)
 *   - Mount point UID (8 bytes)
 *   - Extra data value (4 bytes)
 *
 * Event data is 20 bytes total (two UIDs + one uint32).
 *
 * Parameters:
 *   audit_type - Audit subtype (e.g., 0x1C=add mount, 0x1D=drop mount)
 *   status     - Operation status (0 = success)
 *   uid        - Directory UID
 *   mount_uid  - Mount point UID
 *   extra      - Additional flags/data
 *
 * Original address: 0x00E4BCE0
 * Size: 102 bytes
 */

#include "audit/audit_internal.h"
#include "audit/audit.h"

void audit_$log_mount_op(uint16_t audit_type, status_$t status, uid_t *uid,
                         uid_t *mount_uid, uint32_t extra)
{
    /* Event header (8 bytes, same layout as uid_t) */
    struct {
        uint16_t event_class;       /* Always 4 for directory ops */
        uint16_t audit_subtype;
        uint32_t reserved;
    } event_header;

    /* Event data buffer (20 bytes) */
    struct {
        uid_t    dir_uid;           /* 0x00: Directory UID (8 bytes) */
        uid_t    mount_uid;         /* 0x08: Mount point UID (8 bytes) */
        uint32_t extra;             /* 0x10: Extra data (4 bytes) */
    } event_data;

    uint16_t success_flag;
    static const uint16_t data_len = 20;    /* 0x14 = sizeof(event_data) */

    /* Initialize event header */
    event_header.event_class = 4;
    event_header.audit_subtype = audit_type;
    event_header.reserved = 0;

    /* Copy UIDs into event data */
    event_data.dir_uid.high = uid->high;
    event_data.dir_uid.low = uid->low;
    event_data.mount_uid.high = mount_uid->high;
    event_data.mount_uid.low = mount_uid->low;
    event_data.extra = extra;

    /* Success flag: 1 if status is non-zero (failure), 0 if OK */
    success_flag = (status != 0) ? 1 : 0;

    /* Log the event */
    AUDIT_$LOG_EVENT((uid_t *)&event_header, &success_flag,
                     (uint32_t *)&status, (char *)&event_data, (uint16_t *)&data_len);
}
