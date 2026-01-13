// OS_$GET_EC - Get the shutdown eventcount
// Address: 0x00e6d6b8
// Size: 62 bytes
//
// Returns a registered eventcount that can be used to monitor
// the system shutdown state.

#include "os.h"

// External eventcount registration function
extern void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec, status_$t *status);

// The shutdown eventcount (at 0xe1dc00)
ec_$eventcount_t OS_$SHUTDOWN_EC;

void OS_$GET_EC(void *param_1, ec_$eventcount_t **ec_ret, status_$t *status)
{
    void *registered_ec;
    status_$t local_status;

    // Register the shutdown eventcount
    registered_ec = EC2_$REGISTER_EC1(&OS_$SHUTDOWN_EC, status);

    // Return the registered eventcount
    *ec_ret = (ec_$eventcount_t *)registered_ec;

    // Adjust status - set high bit if non-zero status
    local_status = *status;
    *(uint8_t *)status &= 0x7F;  // Clear high bit
    if (local_status != 0) {
        *(uint8_t *)status |= 0x80;  // Set high bit if error
    }
}
