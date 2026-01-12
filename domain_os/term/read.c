#include "term.h"

// DTTE structure offsets
#define DTTE_SIZE       0x38
#define DTTE_OFFSET_C6  0x12D6  // terminal flags (byte at +0x36 from DTTE_BASE + 0x12A0)

// External data
extern char TERM_$FLAGS_BASE[];  // at 0xe2dc90

// Flag addresses (relative PC offsets in original)
// These are used to select between conditional and blocking reads
static char blocking_read_flag;   // 0xe66898
static char cond_read_flag;       // 0xe66896

// External functions
extern unsigned short TTY_$K_GET(short *line_ptr, char *mode_flag, unsigned int buffer,
                                  void *param3, status_$t *status_ret);

// Reads from a terminal line.
//
// Determines whether to use blocking or conditional read based on
// the terminal's flag settings, then calls TTY_$K_GET.
unsigned short TERM_$READ(short *line_ptr, unsigned int buffer, void *param3,
                          status_$t *status_ret) {
    short real_line;
    unsigned short result;
    int dtte_offset;
    char *mode_flag;

    real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return buffer & 0xFFFF;
    }

    dtte_offset = (short)(real_line * DTTE_SIZE);

    // Check flag at offset 0x36 in the DTTE flags area
    // If high bit set, use conditional read mode
    if ((char)(TERM_$FLAGS_BASE[dtte_offset + 0x36]) < 0) {
        mode_flag = &cond_read_flag;
    } else {
        mode_flag = &blocking_read_flag;
    }

    result = TTY_$K_GET(line_ptr, mode_flag, buffer, param3, status_ret);
    TERM_$STATUS_CONVERT(status_ret);

    return result;
}
