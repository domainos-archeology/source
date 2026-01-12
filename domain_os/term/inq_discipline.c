#include "term.h"

// External data
extern term_data_t TERM_$DATA;  // at 0xe2c9f0

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
