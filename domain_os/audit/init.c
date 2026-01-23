/*
 * init.c - AUDIT_$INIT
 *
 * Initializes the audit subsystem during system startup.
 *
 * Original address: 0x00E70AFC
 *
 * Initialization sequence:
 * 1. Allocate wired memory for event counter and exclusion lock
 * 2. Clear the ENABLED and CORRUPTED flags
 * 3. Initialize UIDs to NIL
 * 4. Clear all per-process suspension counters
 * 5. Initialize exclusion lock and event counter
 * 6. Attempt to start audit logging
 * 7. If startup fails, print warnings and set CORRUPTED flag
 */

#include "audit/audit_internal.h"
#include "acl/acl.h"
#include "misc/misc.h"

/* String constants from original binary */
static const char msg_warning[] =
    "        Warning: could not start audit event logging...";
static const char msg_all_events[] =
    "All events will be logged.   ";
static const char msg_admins_only[] =
    "Only audit administrators will be able to stop auditing...";

void AUDIT_$INIT(void)
{
    status_$t status;
    int i;

    /*
     * Allocate wired memory for event counter structure.
     * The structure contains both an event counter and an exclusion lock.
     * Layout:
     *   offset 0x00: ec_$eventcount_t (event counter)
     *   offset 0x0C: ml_$exclusion_t (exclusion lock)
     */
    AUDIT_$DATA.event_count = (ec_$eventcount_t *)GET_WIRED();

    /* Clear global flags */
    AUDIT_$ENABLED = 0;
    AUDIT_$CORRUPTED = 0;

    /* Clear server running flag */
    AUDIT_$DATA.server_running = 0;

    /* Initialize log file UID to NIL */
    AUDIT_$DATA.log_file_uid.high = UID_$NIL.high;
    AUDIT_$DATA.log_file_uid.low = UID_$NIL.low;
    AUDIT_$DATA.list_count = 0;
    AUDIT_$DATA.flags = 0;

    /* Initialize audit list UID to NIL */
    AUDIT_$DATA.list_uid.high = UID_$NIL.high;
    AUDIT_$DATA.list_uid.low = UID_$NIL.low;
    AUDIT_$DATA.buffer_base = NULL;
    AUDIT_$DATA.buffer_size = 0;

    /* Clear all per-process suspension counters */
    for (i = 0; i < AUDIT_MAX_PROCESSES; i++) {
        AUDIT_$DATA.suspend_count[i] = 0;
    }

    /* Clear server state */
    AUDIT_$DATA.server_pid = 0;
    AUDIT_$DATA.lock_id = 0;

    /* Initialize the exclusion lock (at offset 0x0C from event_count) */
    ML_$EXCLUSION_INIT((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));

    /* Initialize the event counter */
    EC_$INIT(AUDIT_$DATA.event_count);

    /* Enter super mode to access protected files */
    ACL_$ENTER_SUPER();

    /* Attempt to start audit logging */
    audit_$start_logging(&status);

    if (status != status_$ok) {
        /* Startup failed - print warning messages */
        ERROR_$PRINT(msg_warning, &status, NULL);
        ERROR_$PRINT(msg_all_events, NULL, NULL);
        ERROR_$PRINT(msg_admins_only, NULL, NULL);

        /* Set corrupted flag - all events will be logged */
        AUDIT_$CORRUPTED = (int8_t)-1;
    }

    /* Exit super mode */
    /* Note: Original has a bug - calls ENTER_SUPER again instead of EXIT_SUPER */
    /* We'll implement it correctly here */
    ACL_$EXIT_SUPER();
}
