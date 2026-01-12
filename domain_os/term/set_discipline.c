#include "term.h"

// Sets the terminal discipline for a logical line.
//
// First translates the logical line to a real line using TERM_$GET_REAL_LINE,
// then calls TERM_$SET_REAL_LINE_DISCIPLINE to actually set the discipline.
void TERM_$SET_DISCIPLINE(short *line_ptr, void *discipline, status_$t *status_ret) {
    short real_line;

    real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
    if (*status_ret == status_$ok) {
        TERM_$SET_REAL_LINE_DISCIPLINE(&real_line, discipline, status_ret);
    }
}
