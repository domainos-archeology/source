// OS_$GET_EC - Get the shutdown eventcount
// Address: 0x00e6d6b8
// Size: 62 bytes
//
// Returns a registered eventcount that can be used to monitor
// the system shutdown state.

#include "os/os_internal.h"

// The shutdown eventcount (at 0xe1dc00) is defined in os_data.c

void OS_$GET_EC(void *param_1, ec_$eventcount_t **ec_ret, status_$t *status)
{
    void *registered_ec;
    status_$t local_status;

    // Register the shutdown eventcount
    registered_ec = EC2_$REGISTER_EC1(&OS_$SHUTDOWN_EC, status);

    // Return the registered eventcount
    *ec_ret = (ec_$eventcount_t *)registered_ec;

    // Adjust status - set high bit if non-zero status
    // Original: tst.l (A2); sne D0; andi.b #0x7f,(A2); andi.b #0x80,D0; or.b D0,(A2)
    // The byte operations act on the first (most significant) byte of the
    // big-endian 32-bit status, i.e. bit 31 of the status_$t.
    local_status = *status;
    *status &= ~0x80000000;  // Clear high bit
    if (local_status != 0) {
        *status |= 0x80000000;  // Set high bit if error
    }
}
