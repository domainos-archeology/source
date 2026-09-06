/*
 * AUDIT_$LOG_CNAME_OP - Log change name (rename) audit event
 *
 * Logs an audit event for directory rename (CNAMEU) operations.
 * Constructs an audit record containing:
 *   - Event type (4) and audit subtype
 *   - Directory UID
 *   - Old name (null-terminated)
 *   - New name (null-terminated)
 *
 * The two name strings are concatenated with null terminators in the
 * event data buffer.
 *
 * Parameters:
 *   audit_type    - Audit subtype (e.g., 0x18 for CNAMEU)
 *   status        - Operation status to log
 *   uid           - UID of the directory
 *   name_len      - Length of old name
 *   new_name_len  - Length of new name (0 treated as 0)
 *   name          - Old name string
 *   new_name      - New name string
 *
 * Original address: 0x00E4BEC2
 * Original size: 208 bytes
 */

#include "audit/audit_internal.h"
#include "audit/audit.h"
#include "os/os.h"

void AUDIT_$LOG_CNAME_OP(uint16_t audit_type, status_$t status, uid_t *uid,
                         uint16_t name_len, uint16_t new_name_len,
                         void *name, void *new_name)
{
    /* Event header */
    struct {
        uint16_t event_class;       /* Always 4 for directory ops */
        uint16_t audit_subtype;
        uint32_t reserved;
    } event_header;

    /* Event data buffer */
    struct {
        uid_t    dir_uid;           /* 8 bytes */
        char     names[512];        /* Old name + null + new name + null */
    } event_data;

    uint16_t actual_name_len;
    uint16_t actual_new_len;
    char *dst;
    uint16_t data_len;
    uint16_t success_flag;

    /* Initialize event header */
    event_header.event_class = 4;
    event_header.audit_subtype = audit_type;
    event_header.reserved = 0;

    /* Copy directory UID */
    event_data.dir_uid.high = uid->high;
    event_data.dir_uid.low = uid->low;

    /* Clamp old name length (use unsigned compare, take max of 0 and len) */
    actual_name_len = 0;
    if (actual_name_len <= name_len) {
        actual_name_len = name_len;
    }

    /* Handle new name length: 0 stays 0 */
    actual_new_len = new_name_len;
    if (new_name_len == 0) {
        actual_new_len = 0;
    }

    /* Position for new name: after old name + null terminator */
    dst = event_data.names + (uint32_t)actual_name_len + 1;

    /* Copy old name if present */
    if (actual_name_len != 0) {
        OS_$DATA_COPY(name, event_data.names, (uint32_t)actual_name_len);
    }

    /* Null-terminate old name */
    event_data.names[actual_name_len] = '\0';

    /* Copy new name if present */
    if (actual_new_len != 0) {
        OS_$DATA_COPY(new_name, dst, (uint32_t)actual_new_len);
    }

    /* Null-terminate new name */
    dst[actual_new_len] = '\0';

    /* Calculate total data length */
    data_len = (uint16_t)((dst + actual_new_len) - (char *)&event_data.dir_uid) + 1;

    /* Success flag: 1 if status is non-zero (failure), 0 if OK */
    success_flag = (status != 0) ? 1 : 0;

    /* Log the event */
    AUDIT_$LOG_EVENT((uid_t *)&event_header, &success_flag,
                     (uint32_t *)&status, (char *)&event_data.dir_uid, &data_len);
}
