/*
 * log_event.c - AUDIT_$LOG_EVENT
 *
 * Logs an audit event by retrieving the SID for the current process
 * and calling AUDIT_$LOG_EVENT_S.
 *
 * Original address: 0x00E70DF6
 */

#include "audit/audit_internal.h"
#include "acl/acl.h"

void AUDIT_$LOG_EVENT(uid_t *event_uid, uint16_t *event_flags,
                      uint32_t *status, char *data, uint16_t *data_len)
{
    uint8_t sid_buffer[40];
    status_$t local_status;

    /* Only proceed if auditing is enabled */
    if (AUDIT_$ENABLED < 0) {
        /* Get SID for current process */
        ACL_$GET_PID_SID(PROC1_$CURRENT, (uid_t *)sid_buffer, &local_status);

        if (local_status == status_$ok) {
            /* Log the event with the retrieved SID */
            AUDIT_$LOG_EVENT_S(event_uid, event_flags, sid_buffer,
                               status, data, data_len);
        }
    }
}
