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
/*
 * 0x00E5DD06: the audit record's length, 0x003E, as a constant CELL in the
 * code segment.  The routine does not compute it - it takes its address with
 * "pea (0x1c,PC)" at 0x00E5DCE8 (PC = 0x00E5DCEA) and hands the pointer
 * straight to AUDIT_$LOG_EVENT.  Image bytes: 00 3e.  (source-l8qy)
 */
static const uint16_t file_$audit_set_prot_len_00e5dd06 =
    sizeof(file_$audit_set_prot_data_t);

void FILE_$AUDIT_SET_PROT(uid_t *file_uid, void *acl_data, void *prot_info,
                          uint16_t prot_type, status_$t status)
{
    uid_t event_uid;                        /* A6-0x10 */
    uint16_t event_flags;                   /* A6-0x56 */
    file_$audit_set_prot_data_t data;       /* A6-0x50 */
    int16_t i;
    const uint32_t *src;

    /* 0x00E5DC9E / 0x00E5DCA6 */
    event_uid.high = FILE_AUDIT_EVENT_SET_PROT;
    event_uid.low = 0;

    /* 0x00E5DCAE-0x00E5DCB6: eleven longwords, "moveq #0xa" then dbf */
    src = (const uint32_t *)acl_data;
    for (i = 0; i <= 0xA; i++) {
        ((uint32_t *)data.acl_data)[i] = src[i];
    }

    /* 0x00E5DCBA-0x00E5DCC2 */
    data.file_uid = *file_uid;

    /* 0x00E5DCC6-0x00E5DCCE */
    ((uint32_t *)data.prot_info)[0] = ((const uint32_t *)prot_info)[0];
    ((uint32_t *)data.prot_info)[1] = ((const uint32_t *)prot_info)[1];

    /* 0x00E5DCD2 */
    data.prot_type = prot_type;

    /* 0x00E5DCD6-0x00E5DCE6 */
    event_flags = (status != status_$ok) ? 1 : 0;

    /*
     * 0x00E5DCE8-0x00E5DCFC.  The status pointer is the ADDRESS of this
     * routine's own status argument slot ("pea (0x16,A6)"), and the length is
     * the shared constant cell, not a computed value.
     */
    AUDIT_$LOG_EVENT(&event_uid, &event_flags, &status, (char *)&data,
                     &file_$audit_set_prot_len_00e5dd06);
}
