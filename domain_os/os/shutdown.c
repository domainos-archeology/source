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

/*
 * Constant cell in this module's code region, passed to
 * NETWORK_$SET_SERVICE by reference:
 *   0x00E6D5EC "pea (0x3c,PC)" -> 0x00E6D5EE + 0x3C = 0x00E6D62A, a word
 *   holding 0x0002 (read with `gsk read 00e6d62a`), i.e.
 *   NETWORK_OP_SET_VALUE.  Read-only in the original image; declared
 *   non-const here only because NETWORK_$SET_SERVICE's first parameter is
 *   a plain int16_t * (it only reads it, 0x00E0F478 "move.w (A0),D2w").
 */
static int16_t os_$shutdown_net_op_00e6d62a = NETWORK_OP_SET_VALUE;

// Shutdown flag: OS_$SHUTTING_DOWN_FLAG is defined in os_data.c (0xE82734)

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

    // Unlock all files (pea (0xb6,PC) = the constant word 0 at 0xE6D628,
    // the same cell passed to TIME_$WAIT as the delay type above)
    FILE_$PRIV_UNLOCK_ALL(&wait_delay_type);

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

    // Clear the allowed-service mask
    {
        /*
         * 0x00E6D5E0-0x00E6D5F0:
         *   clr.l (-0x24c,A6)   ; the new service value, a longword zero
         *   pea (-0x254,A6)     ; status_p
         *   pea (-0x24c,A6)     ; value_ptr
         *   pea (0x3c,PC)       ; op_ptr -> the constant word 2
         * All three arguments are addresses; the first is NOT the shutdown
         * wait_duration record.
         */
        uint32_t svc_value = 0;
        NETWORK_$SET_SERVICE(&os_$shutdown_net_op_00e6d62a, &svc_value,
                             &local_status);
    }

    // Spin/delay loop before final crash
    for (i = 0x7D0; i >= 0; i--) {
        local_status = M$MIS$LLL(local_status, local_status);
    }

    // Final system halt
    CRASH_SYSTEM(&status);
}
