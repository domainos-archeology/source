// OS_$SHUTDOWN - Perform system shutdown
// Address: 0x00e6d476
// Size: 434 bytes
//
// Shuts down all subsystems in order and halts the system.
// Only allowed for superuser (PROC1_$CURRENT == 1) or locksmith.

#include "os.h"

// External process identifiers
extern short PROC1_$CURRENT;

// External UIDs
extern uid_t RGYC_$G_LOCKSMITH_UID;

// External flags
extern char NETWORK_$REALLY_DISKLESS;
extern char PMAP_$SHUTTING_DOWN_FLAG;
extern m68k_ptr_t FP_$SAVEP;

// External subsystem shutdown functions
extern void NETWORK_$DISMISS_REQUEST_SERVERS(void);
extern void PROC2_$SHUTDOWN(void);
extern void ROUTE_$SHUTDOWN(void);
extern void PACCT_$SHUTDN(void);
extern void LOG_$SHUTDN(void);
extern void AUDIT_$SHUTDOWN(void);
extern void HINT_$SHUTDN(void);
extern void CAL_$SHUTDOWN(status_$t *status);
extern void AREA_$SHUTDOWN(void);
extern status_$t VOLX_$SHUTDOWN(void);
extern void FILE_$PRIV_UNLOCK_ALL(const void *param);

// External ACL functions
extern void ACL_$GET_RE_SIDS(void *buf, uid_t *uid, status_$t *status);

// External display and time functions
extern void CRASH_SHOW_STRING(const char *str);

// External network functions
extern void NETWORK_$SET_SERVICE(const void *param1, status_$t *param2, status_$t *status);

// External formatting
extern void VFMT_$FORMATN(const char *format, char *buf, short *len_ptr, ...);

// Code and data pointers for wiring
extern m68k_ptr_t PTR_OS_PROC_SHUTWIRED;
extern m68k_ptr_t PTR_OS_PROC_SHUTWIRED_END;
extern m68k_ptr_t PTR_OS_DATA_SHUTWIRED;
extern m68k_ptr_t PTR_OS_DATA_SHUTWIRED_END;

// Static data for shutdown
static const char wait_duration[] = { 0, 0 };

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
    TIME_$WAIT(wait_duration, wait_duration, &local_status);

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
                   wire_buf, wait_duration, wire_buf);
    MST_$WIRE_AREA(&PTR_OS_DATA_SHUTWIRED, &PTR_OS_DATA_SHUTWIRED_END,
                   wire_buf, wait_duration, wire_buf);

    // Unlock all files
    FILE_$PRIV_UNLOCK_ALL(wait_duration);

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
            err_len = 104;
            VFMT_$FORMATN("shutdown failed, status =  lh    ",
                          err_buf, &err_len, status);
            CRASH_SHOW_STRING(err_buf);
        }
    }

    CRASH_SHOW_STRING("Shutdown successful.");

    // Clear service status
    {
        status_$t svc_status = 0;
        NETWORK_$SET_SERVICE(wait_duration, &svc_status, &local_status);
    }

    // Spin/delay loop before final crash
    for (i = 0x7D0; i >= 0; i--) {
        local_status = M_MIS_LLL(local_status, local_status);
    }

    // Final system halt
    CRASH_SYSTEM(&status);
}
