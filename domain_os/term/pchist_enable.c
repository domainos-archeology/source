#include "term.h"

// External data
extern term_data_t TERM_$DATA;  // at 0xe2c9f0

// Enables or disables process history tracking.
//
// Stores the enable value in the pchist_enable field and returns success.
void TERM_$PCHIST_ENABLE(unsigned short *enable_ptr, status_$t *status_ret) {
    *status_ret = status_$ok;
    TERM_$DATA.pchist_enable = *enable_ptr;
}
