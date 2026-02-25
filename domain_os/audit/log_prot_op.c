/*
 * audit_$log_prot_op - Log protection operation audit event
 *
 * Logs an audit event for protection-related operations (e.g., ACL changes).
 * Constructs an audit record containing:
 *   - Event type (4) and hardcoded audit subtype (0x14 = 20)
 *   - Protection data buffer (44 bytes)
 *   - Directory UID (8 bytes)
 *   - Subject UID (8 bytes)
 *   - ACL UID (8 bytes)
 *   - Protection flags word (2 bytes)
 *
 * Event data is 70 bytes total (0x46).
 *
 * Note: Unlike other audit_$log_*_op functions, the audit subtype is
 * hardcoded to 0x14 rather than passed as a parameter.
 *
 * Parameters:
 *   status      - Operation status (0 = success)
 *   uid         - Directory UID
 *   prot_data   - Protection data buffer (44 bytes, 11 longwords)
 *   acl_uid     - ACL UID
 *   subject_uid - Subject UID
 *   prot_flags  - Protection flags
 *
 * Original address: 0x00E4AF28
 * Size: 126 bytes
 */

#include "audit/audit.h"
#include "os/os.h"

/* Size of the protection data buffer in bytes (11 longwords) */
#define PROT_DATA_SIZE 44

void audit_$log_prot_op(status_$t status, uid_t *uid, void *prot_data,
                        uid_t *acl_uid, uid_t *subject_uid, uint16_t prot_flags)
{
    /* Event header (8 bytes, same layout as uid_t) */
    struct {
        uint16_t event_class;       /* Always 4 for directory ops */
        uint16_t audit_subtype;     /* Hardcoded to 0x14 for protection ops */
        uint32_t reserved;
    } event_header;

    /*
     * Event data buffer (70 bytes = 0x46)
     *
     * Layout matches the original stack frame:
     *   [0x00..0x2B] protection data (44 bytes)
     *   [0x2C..0x33] directory UID (8 bytes)
     *   [0x34..0x3B] subject UID (8 bytes)
     *   [0x3C..0x43] ACL UID (8 bytes)
     *   [0x44..0x45] protection flags (2 bytes)
     */
    struct {
        uint8_t  prot_data[PROT_DATA_SIZE]; /* Protection data (44 bytes) */
        uid_t    dir_uid;                   /* Directory UID */
        uid_t    subject_uid;               /* Subject UID */
        uid_t    acl_uid;                   /* ACL UID */
        uint16_t prot_flags;                /* Protection flags */
    } event_data;

    uint16_t success_flag;
    static const uint16_t data_len = 70;    /* 0x46 = sizeof(event_data) */

    /* Initialize event header - subtype is hardcoded to 0x14 */
    event_header.event_class = 4;
    event_header.audit_subtype = 0x14;
    event_header.reserved = 0;

    /* Copy protection data (44 bytes = 11 longwords) */
    OS_$DATA_COPY(prot_data, event_data.prot_data, PROT_DATA_SIZE);

    /* Copy UIDs into event data */
    event_data.dir_uid.high = uid->high;
    event_data.dir_uid.low = uid->low;

    /* Note: subject_uid and acl_uid are stored in event data in
     * reversed order relative to their parameter positions.
     * This matches the original assembly at 0x00E4AF5A-0x00E4AF6E. */
    event_data.subject_uid.high = subject_uid->high;
    event_data.subject_uid.low = subject_uid->low;
    event_data.acl_uid.high = acl_uid->high;
    event_data.acl_uid.low = acl_uid->low;

    event_data.prot_flags = prot_flags;

    /* Success flag: 1 if status is non-zero (failure), 0 if OK */
    success_flag = (status != 0) ? 1 : 0;

    /* Log the event */
    AUDIT_$LOG_EVENT((uid_t *)&event_header, &success_flag,
                     (uint32_t *)&status, (char *)&event_data, (uint16_t *)&data_len);
}
