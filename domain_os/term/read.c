#include "term/term_internal.h"

// Flag addresses used to select between conditional and blocking reads
static char blocking_read_flag;   // 0xe66898
static char cond_read_flag;       // 0xe66896

// Reads from a terminal line.
//
// Determines whether to use blocking or conditional read based on
// the terminal's flag settings, then calls TTY_$K_GET.
unsigned short TERM_$READ(short *line_ptr, void *buffer, void *param3,
                          status_$t *status_ret) {
    short real_line;
    unsigned short result;
    char *mode_flag;
    dtte_t *dtte;

    real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return 0;
    }

    dtte = &TERM_$DATA.dtte[real_line];

    // Check flag - if high bit set (negative), use conditional read mode
    if ((signed char)dtte->flags < 0) {
        mode_flag = &cond_read_flag;
    } else {
        mode_flag = &blocking_read_flag;
    }

    result = TTY_$K_GET(line_ptr, mode_flag, buffer, param3, status_ret);
    TERM_$STATUS_CONVERT(status_ret);

    return result;
}
