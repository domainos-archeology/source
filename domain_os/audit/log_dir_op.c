/*
 * AUDIT_$LOG_DIR_OP - Log directory operation audit event
 *
 * Logs an audit event for directory operations such as add/drop entry.
 * Constructs an audit record containing:
 *   - Event type (4) and audit subtype
 *   - Directory UID
 *   - File/object UID
 *   - Entry name
 *
 * Parameters:
 *   audit_type - Audit subtype (e.g., 0x12=add, 0x13=drop, etc.)
 *   status     - Operation status to log
 *   dir_uid    - UID of the directory
 *   file_uid   - UID of the file/object
 *   name_len   - Length of entry name (may be negative = 0)
 *   name       - Entry name string
 *
 * Original address: 0x00e4be16
 * Size: 172 bytes
 */

#include "audit/audit.h"
#include "os/os.h"

void AUDIT_$LOG_DIR_OP(uint16_t audit_type, status_$t status, uid_t *dir_uid,
                       uid_t *file_uid, uint16_t name_len, void *name)
{
    /* Event header */
    struct {
        uint16_t event_class;   /* 0x00: Always 4 for directory ops */
        uint16_t audit_subtype; /* 0x02: audit_type parameter */
        uint32_t reserved;      /* 0x04: Always 0 */
    } event_header;

    /* Event data buffer */
    struct {
        uid_t    dir_uid;       /* 0x00: Directory UID (8 bytes) */
        uid_t    file_uid;      /* 0x08: File/object UID (8 bytes) */
        char     name[256];     /* 0x10: Entry name (null-terminated) */
    } event_data;

    uint16_t actual_len;
    uint16_t data_len;
    uint16_t success_flag;

    /* Initialize event header */
    event_header.event_class = 4;
    event_header.audit_subtype = audit_type;
    event_header.reserved = 0;

    /* Clamp name length to non-negative */
    actual_len = 0;
    if ((int16_t)name_len > 0) {
        actual_len = name_len;
    }

    /* Copy UIDs */
    event_data.dir_uid.high = dir_uid->high;
    event_data.dir_uid.low = dir_uid->low;
    event_data.file_uid.high = file_uid->high;
    event_data.file_uid.low = file_uid->low;

    /* Copy name if present */
    if (actual_len != 0) {
        OS_$DATA_COPY(name, event_data.name, actual_len);
    }

    /* Null-terminate the name */
    event_data.name[actual_len] = '\0';

    /* Calculate total data length:
     * UIDs (16 bytes) + name string + null terminator + 1
     * The +1 seems to account for alignment or end marker
     */
    data_len = ((char *)&event_data.name[actual_len] - (char *)&event_data) + 1;

    /* Success flag: 1 if status is non-zero (failure), 0 if OK */
    success_flag = (status != 0) ? 1 : 0;

    /* Log the event */
    AUDIT_$LOG_EVENT((uid_t *)&event_header, &success_flag,
                     (uint32_t *)&status, (char *)&event_data, &data_len);
}
