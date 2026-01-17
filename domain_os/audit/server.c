/*
 * server.c - AUDIT_$SERVER
 *
 * Background server process for the audit subsystem.
 * Waits for events and periodically flushes the audit buffer.
 *
 * Original address: 0x00E710C6
 */

#include "audit/audit_internal.h"
#include "acl/acl.h"
#include "time/time.h"
#include "file/file.h"

void AUDIT_$SERVER(void)
{
    int16_t wait_count;
    status_$t local_status;
    ec_$eventcount_t *event_counts[1];
    int32_t *clock_ptr;
    int32_t wait_values[1];
    int32_t timeout_time;
    uint16_t wake_reason;

    /* Mark this process as suspended from auditing */
    AUDIT_$DATA.suspend_count[PROC1_$CURRENT] = 1;

    /* Mark server as running */
    AUDIT_$DATA.server_running = (uint8_t)-1;

    /* Set up event count array for waiting */
    event_counts[0] = AUDIT_$DATA.event_count;
    clock_ptr = &TIME_$CLOCKH;

    /* Enter super mode for file access */
    ACL_$ENTER_SUPER();

    /* Main loop - run while auditing is enabled */
    while (AUDIT_$ENABLED < 0) {
        /* Determine wait parameters */
        wait_count = 1;

        ML_$EXCLUSION_START((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

        if ((AUDIT_$DATA.flags & AUDIT_FLAG_TIMEOUT) != 0) {
            /* Periodic flush is enabled */
            if (AUDIT_$DATA.timeout == 0) {
                /* Use default timeout (8 minutes = 0x1E0 4-second units) */
                timeout_time = TIME_$CLOCKH + AUDIT_DEFAULT_TIMEOUT;
            } else {
                /* Use configured timeout (multiply by 4 for 4-second units) */
                timeout_time = TIME_$CLOCKH + (AUDIT_$DATA.timeout * 4);
            }
            wait_count = 2;  /* Wait on event count OR timeout */
        }

        ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

        /* Wait for event count to advance or timeout */
        wait_values[0] = AUDIT_$DATA.event_count->value + 1;

        wake_reason = EC_$WAITN(
            (ec_$eventcount_t **)event_counts,
            wait_values,
            wait_count
        );

        ML_$EXCLUSION_START((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

        /* Check if we woke up due to timeout and buffer is dirty */
        if (wake_reason == 2) {
            /* Timeout - check if we need to flush */
            if (AUDIT_$DATA.log_file_uid.high != UID_$NIL.high ||
                AUDIT_$DATA.log_file_uid.low != UID_$NIL.low) {
                /* Log file is open */
                if (AUDIT_$DATA.dirty < 0) {
                    /* Buffer has unwritten data - flush it */
                    AUDIT_$DATA.dirty = 0;
                    FILE_$FW_FILE(&AUDIT_$DATA.log_file_uid, &local_status);
                }
            }
        }

        ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));
    }

    /* Server is stopping */
    AUDIT_$DATA.server_running = 0;

    /* Exit super mode */
    ACL_$EXIT_SUPER();

    /* Unbind from process table */
    PROC1_$UNBIND(AUDIT_$DATA.server_pid, &local_status);
}
