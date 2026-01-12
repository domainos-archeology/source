#include "term.h"

// DTTE offsets for eventcount pointers
#define DTTE_SIZE           0x38
#define DTTE_EC_INPUT_OFFSET   0x12AC  // offset 0x0c from flags base (0xe2dc9c)
#define DTTE_EC_OUTPUT_OFFSET  0x12B8  // offset 0x18 from flags base (0xe2dca8)

// External data
extern char TERM_$FLAGS_BASE[];  // at 0xe2dc90

// External functions
extern void *EC2_$REGISTER_EC1(void *ec_ptr, status_$t *status_ret);

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
    int dtte_offset;
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

    dtte_offset = (unsigned int)line * DTTE_SIZE;

    if (*ec_id == 0) {
        // Input eventcount
        ec_ptr = (void *)(TERM_$FLAGS_BASE + dtte_offset + 0x0c);
    } else if (*ec_id == 1) {
        // Output eventcount
        ec_ptr = (void *)(TERM_$FLAGS_BASE + dtte_offset + 0x18);
    } else {
        return;
    }

    registered_ec = EC2_$REGISTER_EC1(ec_ptr, status_ret);
    ec_ret->value = (long)registered_ec;
}
