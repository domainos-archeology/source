#include "term.h"

// Mode flag for write operations
static char write_mode_flag;  // 0xe66898

// External functions
extern void TTY_$K_PUT(void *line_ptr, char *mode_flag, void *buffer,
                       unsigned short *count_ptr, status_$t *status_ret);

// Writes to a terminal line.
//
// Copies the count value to a local variable (stack), then calls
// TTY_$K_PUT with the standard write mode flag.
void TERM_$WRITE(void *line_ptr, void *buffer, unsigned short *count_ptr,
                 status_$t *status_ret) {
    unsigned short count;

    count = *count_ptr;
    TTY_$K_PUT(line_ptr, &write_mode_flag, buffer, &count, status_ret);
    TERM_$STATUS_CONVERT(status_ret);
}
