#include "term.h"

// Global variables
extern short DTTY_$CTRL;           // at 0xe2e00e - default TTY control line
extern short PROC1_$CURRENT;       // at 0xe20608 - current process indicator
extern term_data_t TERM_$DATA;     // at 0xe2c9f0

// Translates a logical terminal line number to a real line number.
//
// Line mapping:
//   0 -> DTTY_$CTRL (default display TTY control line)
//   1 -> DTTY_$CTRL if PROC1_$CURRENT == 1, else 1
//   other -> passed through unchanged
//
// Valid line numbers are 0-3, but must also be < max_dtte.
//
// Returns: the real line number (or input unchanged on error)
// Sets status_ret to:
//   status_$ok on success
//   status_$invalid_line_number (0xb0007) if line_num > 3
//   status_$requested_line_or_operation_not_implemented (0xb000d) if line_num >= max_dtte
short TERM_$GET_REAL_LINE(short line_num, status_$t *status_ret) {
    *status_ret = status_$ok;

    if (line_num == 1) {
        if (PROC1_$CURRENT == 1) {
            line_num = DTTY_$CTRL;
        }
        // else line_num stays 1
    } else if (line_num == 0) {
        line_num = DTTY_$CTRL;
    }

    if ((unsigned short)line_num > 3) {
        *status_ret = status_$invalid_line_number;
    } else if ((unsigned short)line_num >= TERM_$DATA.max_dtte) {
        *status_ret = status_$requested_line_or_operation_not_implemented;
    }

    return line_num;
}
