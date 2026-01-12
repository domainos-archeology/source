#include "term.h"

// PCHIST (presumably "PC History" or "Process History") enable flag
// at offset 0x1294 from DTTE base (0xe2dc84)
extern char TERM_$DTTE_BASE[];  // at 0xe2c9f0
#define PCHIST_OFFSET  0x1294

// Enables or disables process history tracking.
//
// Stores the enable value in a global flag and returns success.
void TERM_$PCHIST_ENABLE(unsigned short *enable_ptr, status_$t *status_ret) {
    *status_ret = status_$ok;
    *(unsigned short *)(TERM_$DTTE_BASE + PCHIST_OFFSET) = *enable_ptr;
}
