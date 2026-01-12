#include "term.h"

// DTTE structure offsets
#define DTTE_SIZE           0x38
#define DTTE_OFFSET_12D4    0x12D4  // discipline value storage

// External data
extern char TERM_$DTTE_BASE[];  // at 0xe2c9f0

// Inquires the current discipline for a terminal line.
//
// Translates the logical line to a real line, then retrieves
// the discipline value from the DTTE.
void TERM_$INQ_DISCIPLINE(short *line_ptr, unsigned short *discipline_ret,
                          status_$t *status_ret) {
    short real_line;
    int dtte_offset;

    real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
    if (*status_ret == status_$ok) {
        dtte_offset = (short)(real_line * DTTE_SIZE);
        *discipline_ret = *(unsigned short *)(TERM_$DTTE_BASE + dtte_offset + DTTE_OFFSET_12D4);
    }
}
