/*
 * AUDIT_$LOG_LINK_OP - Log link operation audit event
 *
 * Logs an audit event for link operations such as add_link/read_link.
 * Constructs an audit record containing:
 *   - Event type (4) and audit subtype
 *   - Directory UID
 *   - Entry name (null-terminated)
 *   - Target name/path (null-terminated)
 *
 * The two name strings are concatenated with null terminators in the
 * event data buffer. Name lengths are clamped to non-negative values.
 *
 * Parameters:
 *   audit_type  - Audit subtype (e.g., 0x1A=add_link, etc.)
 *   status      - Operation status to log
 *   uid         - UID of the directory
 *   name_len    - Length of entry name (clamped to >= 0)
 *   name        - Entry name string
 *   target_len  - Length of target name (clamped to >= 0)
 *   target_data - Target name/path string
 *
 * Original address: 0x00E4BD48
 * Original size: 206 bytes
 */

#include "audit/audit.h"
#include "os/os.h"

void AUDIT_$LOG_LINK_OP(uint16_t audit_type, status_$t status, uid_t *uid,
                        uint16_t name_len, void *name, uint16_t target_len,
                        void *target_data)
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
        char     names[1280];       /* Entry name + null + target name + null */
    } event_data;

    uint16_t actual_name_len;
    uint16_t actual_target_len;
    char *dst;
    uint16_t data_len;
    uint16_t success_flag;

    /* Initialize event header */
    event_header.event_class = 4;
    event_header.audit_subtype = audit_type;
    event_header.reserved = 0;

    /* Clamp name length to non-negative */
    actual_name_len = 0;
    if ((int16_t)name_len > 0) {
        actual_name_len = name_len;
    }

    /* Clamp target length to non-negative */
    actual_target_len = target_len;
    if ((int16_t)target_len < 0) {
        actual_target_len = 0;
    }

    /* Copy directory UID */
    event_data.dir_uid.high = uid->high;
    event_data.dir_uid.low = uid->low;

    /* Position for target name: after entry name + null terminator */
    dst = event_data.names + (uint32_t)actual_name_len + 1;

    /* Copy entry name if present */
    if (actual_name_len != 0) {
        OS_$DATA_COPY(name, event_data.names, (uint32_t)actual_name_len);
    }

    /* Null-terminate entry name */
    event_data.names[actual_name_len] = '\0';

    /* Copy target name if present */
    if (actual_target_len != 0) {
        OS_$DATA_COPY(target_data, dst, (uint32_t)actual_target_len);
    }

    /* Null-terminate target name */
    dst[actual_target_len] = '\0';

    /* Calculate total data length */
    data_len = (uint16_t)((dst + actual_target_len) - (char *)&event_data.dir_uid) + 1;

    /* Success flag: 1 if status is non-zero (failure), 0 if OK */
    success_flag = (status != 0) ? 1 : 0;

    /* Log the event */
    AUDIT_$LOG_EVENT((uid_t *)&event_header, &success_flag,
                     (uint32_t *)&status, (char *)&event_data.dir_uid, &data_len);
}
