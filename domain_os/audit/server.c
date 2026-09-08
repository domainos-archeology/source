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

/*
 * Frame (link.w A6,-0x28):
 *   A6-0x26  word  the EC_$WAITN count (1 or 2)
 *   A6-0x24  long  status for FILE_$FW_FILE and PROC1_$UNBIND
 *   A6-0x20  the eventcount pointer array: [0] at -0x20, [1] at -0x1C
 *   A6-0x10  the wait-value array:         [0] at -0x10, [1] at -0x0C
 * The two arrays are 0x10 bytes apart, so the Pascal source very likely
 * declared four slots each; only elements 0 and 1 are ever written or read.
 */
void AUDIT_$SERVER(void)
{
    int16_t wait_count;
    status_$t local_status;
    ec_$eventcount_t *event_counts[2];   /* A6-0x20, A6-0x1C */
    int32_t wait_values[2];              /* A6-0x10, A6-0x0C */
    int16_t wake_reason;

    /* 0xE710DC: mark this process as suspended (1-based array) */
    AUDIT_$DATA.suspend_count[PROC1_$CURRENT - 1] = 1;

    /* Mark server as running */
    AUDIT_$DATA.server_running = (uint8_t)-1;

    /*
     * 0x00E710E6-0x00E710F2: both eventcount pointers are laid down once,
     * before the loop.  Slot 1 is the literal 0xE2B0D4 = TIME_$CLOCKH, the
     * system clock eventcount used as the periodic-flush timer.
     */
    event_counts[0] = AUDIT_$DATA.event_count;
    event_counts[1] = (ec_$eventcount_t *)&TIME_$CLOCKH;

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
                /* 0x00E7114A: default timeout, TIME_$CLOCKH + 0x1E0. */
                wait_values[1] = (int32_t)(TIME_$CLOCKH + AUDIT_DEFAULT_TIMEOUT);
            } else {
                /* 0x00E7113E: `ext.l` then `lsl.l #0x2` - a SIGNED word
                 * scaled by 4, added to TIME_$CLOCKH. */
                wait_values[1] = (int32_t)(TIME_$CLOCKH +
                                           (uint32_t)((int32_t)AUDIT_$DATA.timeout * 4));
            }
            wait_count = 2;  /* Wait on event count OR timeout */
        }

        ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

        /* Wait for event count to advance or timeout */
        wait_values[0] = AUDIT_$DATA.event_count->value + 1;

        wake_reason = (int16_t)EC_$WAITN(event_counts, wait_values, wait_count);

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
