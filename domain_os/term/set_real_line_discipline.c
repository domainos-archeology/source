#include "term.h"

/* ML_$SPIN_LOCK, ML_$SPIN_UNLOCK declared in ml/ml.h via term.h */
extern void DTTY_$RELOAD_FONT(void);

// Handler function pointer for SUMA mode
extern void (*SUMA_$RCV)(void);  // at 0xe1ad18

// Keyboard string data
extern char TERM_$KBD_STRING_DATA[];  // at 0xe2dd80
extern char TERM_$KBD_STRING_LEN[];   // at 0xe1ac9c

// Discipline values:
//   0 = TTY mode (normal terminal I/O)
//   1 = disable alternate handler
//   2 = enable alternate handler with font reload
//   3 = SUMA mode (for graphics/display manager)
void TERM_$SET_REAL_LINE_DISCIPLINE(unsigned short *line_ptr, short *discipline_ptr,
                                     status_$t *status_ret) {
    unsigned short line;
    short discipline;
    unsigned short token;
    dtte_t *dtte;
    void **tty_struct;

    line = *line_ptr;
    if (line > 3) {
        *status_ret = status_$invalid_line_number;
        return;
    }

    if (line >= TERM_$DATA.max_dtte) {
        *status_ret = status_$requested_line_or_operation_not_implemented;
        return;
    }

    *status_ret = status_$ok;
    discipline = *discipline_ptr;
    dtte = &TERM_$DATA.dtte[line];

    switch (discipline) {
        case 0:  // TTY mode
        case 3:  // SUMA mode
            if (dtte->tty_handler == 0) {
                *status_ret = status_$invalid_line_number;
                return;
            }
            token = ML_$SPIN_LOCK((void *)(uintptr_t)TERM_$DATA.tty_spin_lock);

            tty_struct = (void **)(uintptr_t)dtte->tty_handler;
            if (discipline == 0) {
                // Set up normal TTY handlers
                tty_struct[1] = (void *)(uintptr_t)dtte->handler_ptr;
                tty_struct[10] = (void *)(uintptr_t)TERM_$DATA.ptr_tty_i_rcv;      // offset 0x28
                tty_struct[11] = (void *)(uintptr_t)TERM_$DATA.ptr_tty_i_drain;    // offset 0x2c
                tty_struct[12] = (void *)(uintptr_t)TERM_$DATA.ptr_tty_i_hup;      // offset 0x30
                tty_struct[13] = (void *)(uintptr_t)TERM_$DATA.ptr_tty_i_int;      // offset 0x34
            } else {
                // SUMA mode - set receive handler
                tty_struct[10] = (void *)SUMA_$RCV;  // offset 0x28
            }
            ML_$SPIN_UNLOCK((void *)(uintptr_t)TERM_$DATA.tty_spin_lock, token);
            break;

        case 1:  // Disable alternate handler
        case 2:  // Enable alternate handler
            if (dtte->alt_handler == 0) {
                *status_ret = status_$invalid_line_number;
                return;
            }
            token = ML_$SPIN_LOCK((void *)(uintptr_t)TERM_$DATA.tty_spin_lock);

            if (discipline == 1) {
                // Disable - clear the handler
                *(m68k_ptr_t *)(uintptr_t)dtte->alt_handler = 0;
            } else {
                // Enable alternate handler with font reload
                *(m68k_ptr_t *)(uintptr_t)dtte->alt_handler = TERM_$DATA.ptr_tty_i_rcv_alt;
                ML_$SPIN_UNLOCK((void *)(uintptr_t)TERM_$DATA.tty_spin_lock, token);
                DTTY_$RELOAD_FONT();
                TERM_$SEND_KBD_STRING(TERM_$KBD_STRING_DATA, TERM_$KBD_STRING_LEN);
                goto store_discipline;
            }
            ML_$SPIN_UNLOCK((void *)(uintptr_t)TERM_$DATA.tty_spin_lock, token);
            break;

        default:
            // Unknown discipline - just store it
            break;
    }

store_discipline:
    // Store the discipline value in the DTTE
    dtte->discipline = discipline;
}
