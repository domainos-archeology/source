#include "term/term_internal.h"

// Mode flag for conditional read
static char cond_read_flag;  // 0xe66896

// Performs a conditional (non-blocking) read from a terminal line.
//
// Unlike TERM_$READ, this always uses conditional mode without
// checking line flags. Returns immediately even if no data is available.
unsigned short TERM_$READ_COND(void *line_ptr, void *buffer, void *param3,
                               status_$t *status_ret) {
    unsigned short result;

    result = TTY_$K_GET(line_ptr, &cond_read_flag, buffer, param3, status_ret);
    TERM_$STATUS_CONVERT(status_ret);

    return result;
}
