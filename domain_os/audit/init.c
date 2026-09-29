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

/*
 * The three `pea (d,PC)` cells the failure path pushes, with their image
 * bytes.  All three are VFMT format strings in the Domain dialect, where
 * "%/" is a newline and "%." ends the format.
 *
 *   0x00E70C40  `pea (0x9e,PC)` at 0x00E70BA0 (extension word 0x00E70BA2)
 *   0x00E70C24  `pea (0x6e,PC)` at 0x00E70BB4 (extension word 0x00E70BB6)
 *   0x00E70BEA  `pea (0x20,PC)` at 0x00E70BC8 (extension word 0x00E70BCA)
 */
static const char audit_$init_warning_fmt[] =
    "%/%/%/%/Warning, could not start audit trail: 0x%8zulh%.";   /* 0x00E70C40 */
static const char audit_$init_all_events_fmt[] =
    "All events will be logged.%.";                              /* 0x00E70C24 */
static const char audit_$init_admins_only_fmt[] =
    "Only audit administrators will be allowed to login.%.";      /* 0x00E70BEA */

/*
 * 0x00E70C20: four zero bytes, the argument-list terminator.  The first
 * VFMT_$WRITE10 call pushes it once (`pea (0x86,PC)` at 0x00E70B98); the
 * second and third push it twice, once with a `pea (d,PC)` and once by
 * duplicating the value already on the stack (`move.l (SP),-(SP)`).
 */
static const uint32_t audit_$init_arg_end = 0x00000000u;

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

    /* 0x00E70B22-0x00E70B2E: audit list UID (A5+0xA0) to NIL. */
    AUDIT_$DATA.list_uid.high = UID_$NIL.high;
    AUDIT_$DATA.list_uid.low = UID_$NIL.low;

    /* 0x00E70B30-0x00E70B36: A5+0xAC then A5+0xA8, in that order. */
    AUDIT_$DATA.list_count = 0;
    AUDIT_$DATA.flags = 0;

    /* 0x00E70B38-0x00E70B44: log file UID (A5+0x80) to NIL. */
    AUDIT_$DATA.log_file_uid.high = UID_$NIL.high;
    AUDIT_$DATA.log_file_uid.low = UID_$NIL.low;

    /* 0x00E70B46-0x00E70B4C: A5+0x88 and A5+0x8C. */
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

    /* 0x00E70B82: enter super mode to access protected files. */
    ACL_$ENTER_SUPER();

    /* Attempt to start audit logging */
    audit_$start_logging(&status);

    if (status != status_$ok) {
        /*
         * 0x00E70B98-0x00E70BD4: three VFMT_$WRITE10 calls.  Arguments are
         * pushed right to left, so the format string is the last push in each
         * group.  The trailing pointers are the 0x00E70C20 terminator cell,
         * not NULL.
         */
        VFMT_$WRITE10(audit_$init_warning_fmt, &status, &audit_$init_arg_end);
        VFMT_$WRITE10(audit_$init_all_events_fmt, &audit_$init_arg_end,
                      &audit_$init_arg_end);
        VFMT_$WRITE10(audit_$init_admins_only_fmt, &audit_$init_arg_end,
                      &audit_$init_arg_end);

        /* 0x00E70BD6: `st` - all events will be logged from now on. */
        AUDIT_$CORRUPTED = (int8_t)-1;
    }

    /*
     * 0x00E70BDC: the image jumps to 0x00E46F90 a SECOND time, i.e. it calls
     * ACL_$ENTER_SUPER again rather than ACL_$EXIT_SUPER (0x00E46FB4).  That
     * leaves ACL_$UNWIRED_DATA.super_count[pid] permanently at 2 for the initialisation
     * process; it looks like an original bug, but the archive reproduces the
     * image, so this is ENTER_SUPER here too.
     */
    ACL_$ENTER_SUPER();
}
