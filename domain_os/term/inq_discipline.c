#include "term/term_internal.h"

// Inquires the current discipline for a terminal line.
//
// Translates the logical line to a real line, then retrieves
// the discipline value from the DTTE.
void TERM_$INQ_DISCIPLINE(short *line_ptr, unsigned short *discipline_ret,
                          status_$t *status_ret) {
    short real_line;

    real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
    if (*status_ret == status_$ok) {
        *discipline_ret = TERM_$DATA.dtte[real_line].discipline;
    }
}
