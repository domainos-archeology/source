#include "term/term_internal.h"

// Enables or disables process history tracking.
//
// Stores the enable value in the pchist_enable field and returns success.
void TERM_$PCHIST_ENABLE(unsigned short *enable_ptr, status_$t *status_ret) {
    *status_ret = status_$ok;
    TERM_$DATA.pchist_enable = *enable_ptr;
}
