/*
 * start_logging.c - audit_$start_logging
 *
 * Starts audit event logging by loading the audit list,
 * opening the log file, and starting the server process.
 *
 * Original address: 0x00E70C78
 */

#include "audit/audit_internal.h"

void audit_$start_logging(status_$t *status_ret)
{
    int8_t list_loaded;
    uint32_t process_flags;

    /* Check if already enabled */
    if (AUDIT_$ENABLED < 0) {
        *status_ret = status_$audit_event_logging_already_started;
        return;
    }

    /* Load the audit list (selective auditing) */
    list_loaded = audit_$load_list(status_ret);

    if (list_loaded < 0 && *status_ret == status_$ok) {
        /* List loaded successfully, now open the log file */
        ML_$EXCLUSION_START((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

        audit_$open_log(status_ret);

        ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

        if (*status_ret == status_$ok) {
            /* Enable auditing */
            AUDIT_$ENABLED = (int8_t)-1;

            /* Start or signal the server process */
            if (AUDIT_$DATA.server_running < 0) {
                /* Server already running, just signal it */
                EC_$ADVANCE(AUDIT_$DATA.event_count);
            } else {
                /* Start a new server process */
                AUDIT_$DATA.server_running = (uint8_t)-1;

                process_flags = AUDIT_SERVER_PROCESS_FLAGS;
                AUDIT_$DATA.server_pid = (int16_t)PROC1_$CREATE_P(
                    AUDIT_$SERVER,
                    process_flags,
                    status_ret
                );

                if (*status_ret != status_$ok) {
                    /* Failed to start server, disable auditing */
                    AUDIT_$DATA.server_running = 0;
                    AUDIT_$ENABLED = 0;
                }
            }
        }
    }
}
