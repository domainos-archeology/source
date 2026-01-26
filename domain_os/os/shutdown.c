// OS_$SHUTDOWN - Perform system shutdown
// Address: 0x00e6d476
// Size: 434 bytes
//
// Shuts down all subsystems in order and halts the system.
// Only allowed for superuser (PROC1_$CURRENT == 1) or locksmith.

#include "os/os_internal.h"

// Static data for shutdown
static uint16_t wait_delay_type = 0;  /* 0 = relative wait */
static clock_t wait_duration = { 0, 0 };

// Shutdown flag
char OS_$SHUTTING_DOWN_FLAG;

void OS_$SHUTDOWN(status_$t *status_p)
{
    status_$t local_status;
    status_$t status;
    uid_t caller_uid;
    char acl_buf[40];
    char wire_buf[400];
    char err_buf[104];
    short err_len;
    short i;

    local_status = *status_p;

    // Check privilege - must be superuser or locksmith
    if (PROC1_$CURRENT == 1) {
        goto do_shutdown;
    }

    // Check if caller is locksmith
    ACL_$GET_RE_SIDS(acl_buf, &caller_uid, &local_status);
    if (local_status != 0) {
        return;  // Not authorized
    }

    if (caller_uid.high != RGYC_$G_LOCKSMITH_UID.high ||
        caller_uid.low != RGYC_$G_LOCKSMITH_UID.low) {
        return;  // Not locksmith
    }

do_shutdown:
    // Wait briefly before starting
    TIME_$WAIT(&wait_delay_type, &wait_duration, &local_status);

    CRASH_SHOW_STRING("Beginning shutdown sequence......");

    // Set shutdown flag
    OS_$SHUTTING_DOWN_FLAG = (char)0xFF;

    // Shutdown network request servers
    NETWORK_$DISMISS_REQUEST_SERVERS();

    // Shutdown process manager
    PROC2_$SHUTDOWN();

    // Shutdown routing
    ROUTE_$SHUTDOWN();

    // Shutdown process accounting
    PACCT_$SHUTDN();

    // Shutdown logging
    LOG_$SHUTDN();

    // Shutdown auditing
    AUDIT_$SHUTDOWN();

    // Shutdown hints
    HINT_$SHUTDN();

    // Shutdown calendar if not diskless
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        CAL_$SHUTDOWN(&local_status);
    }

    // Clear floating point save pointer
    FP_$SAVEP = 0;

    // Wire the shutdown code and data areas
    MST_$WIRE_AREA(&PTR_OS_PROC_SHUTWIRED, &PTR_OS_PROC_SHUTWIRED_END,
                   wire_buf, &wait_duration, wire_buf);
    MST_$WIRE_AREA(&PTR_OS_DATA_SHUTWIRED, &PTR_OS_DATA_SHUTWIRED_END,
                   wire_buf, &wait_duration, wire_buf);

    // Unlock all files
    FILE_$PRIV_UNLOCK_ALL(&wait_duration);

    // Set paging shutting down flag
    PMAP_$SHUTTING_DOWN_FLAG = (char)0xFF;

    // Shutdown areas
    AREA_$SHUTDOWN();

    // Shutdown volumes if not diskless
    status = status_$ok;
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        VOLX_$SHUTDOWN();
        status = VOLX_$SHUTDOWN();
        if (status != 0) {
            int16_t out_len;
            err_len = 104;
            VFMT_$FORMATN("shutdown failed, status =  lh    ",
                          err_buf, &err_len, &out_len, status);
            CRASH_SHOW_STRING(err_buf);
        }
    }

    CRASH_SHOW_STRING("Shutdown successful.");

    // Clear service status
    {
        status_$t svc_status = 0;
        NETWORK_$SET_SERVICE(&wait_duration, &svc_status, &local_status);
    }

    // Spin/delay loop before final crash
    for (i = 0x7D0; i >= 0; i--) {
        local_status = M$MIS$LLL(local_status, local_status);
    }

    // Final system halt
    CRASH_SYSTEM(&status);
}
