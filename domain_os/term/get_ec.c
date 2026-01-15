#include "term/term_internal.h"
#include "ec/ec.h"

// Gets an eventcount for a terminal line.
//
// ec_id specifies which eventcount to retrieve:
//   0 = input eventcount (data available for reading)
//   1 = output eventcount (output buffer drained)
//
// The eventcount is registered with the EC2 subsystem and returned.
void TERM_$GET_EC(unsigned short *ec_id, short *term_line, ec2_$eventcount_t *ec_ret,
                  status_$t *status_ret) {
    unsigned short line;
    void *ec_ptr;
    void *registered_ec;

    if (*ec_id > 1) {
        *status_ret = status_$term_invalid_option;
        return;
    }

    line = TERM_$GET_REAL_LINE(*term_line, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    if (*ec_id == 0) {
        // Cast m68k_ptr_t (uint32_t address) to native pointer
        ec_ptr = (void *)(uintptr_t)TERM_$DATA.dtte[line].input_ec;
    } else if (*ec_id == 1) {
        ec_ptr = (void *)(uintptr_t)TERM_$DATA.dtte[line].output_ec;
    } else {
        return;
    }

    registered_ec = EC2_$REGISTER_EC1(ec_ptr, status_ret);
    ec_ret->value = (long)registered_ec;
}
