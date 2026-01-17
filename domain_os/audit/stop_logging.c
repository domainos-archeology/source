/*
 * stop_logging.c - audit_$stop_logging
 *
 * Stops audit event logging by disabling auditing,
 * signaling the server, and flushing the log file.
 *
 * Original address: 0x00E70D38
 */

#include "audit/audit_internal.h"

void audit_$stop_logging(status_$t *status_ret)
{
    /* Check if auditing is enabled */
    if (AUDIT_$ENABLED >= 0) {
        *status_ret = status_$audit_event_logging_already_stopped;
        return;
    }

    /* Disable auditing */
    AUDIT_$ENABLED = 0;

    /* Signal the server process to stop */
    EC_$ADVANCE(AUDIT_$DATA.event_count);

    /* Flush and close the log file */
    ML_$EXCLUSION_START((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

    audit_$close_log(status_ret);

    ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

    *status_ret = status_$ok;
}
