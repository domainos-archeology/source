#include "term.h"

// DTTE (Display Terminal Table Entry) structure offsets
// Each DTTE is 0x38 (56) bytes
#define DTTE_SIZE           0x38
#define DTTE_OFFSET_12C4    0x12C4  // some handler pointer
#define DTTE_OFFSET_12C8    0x12C8  // TTY handler pointer
#define DTTE_OFFSET_12CC    0x12CC  // alternate handler pointer
#define DTTE_OFFSET_12D0    0x12D0  // another pointer
#define DTTE_OFFSET_12D4    0x12D4  // discipline value storage

// External data
extern char TERM_$DTTE_BASE[];     // at 0xe2c9f0 - base of DTTE array
extern short TERM_$MAX_DTTE;       // at 0xe2dd78
extern void *TTY_$SPIN_LOCK;       // at 0xe2ce74

// Handler function pointers stored in ROM/data
extern void *PTR_TTY_$I_RCV;                           // at 0xe2ca08
extern void *PTR_TTY_$I_OUTPUT_BUFFER_DRAINED;         // at 0xe2ca0c
extern void *PTR_TTY_$I_HUP;                           // at 0xe2ca10
extern void *PTR_TTY_$I_INTERRUPT;                     // at 0xe2ca14
extern void *PTR_TTY_$I_RCV_ALT;                       // at 0xe2cab0
extern void (*SUMA_$RCV)(void);                        // at 0xe1ad18

// External functions
extern unsigned short ML_$SPIN_LOCK(void *lock);
extern void ML_$SPIN_UNLOCK(void *lock, unsigned short token);
extern void DTTY_$RELOAD_FONT(void);

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
    int dtte_offset;
    unsigned short token;
    void **handler_ptr;
    void **alt_handler_ptr;

    line = *line_ptr;
    if (line > 3) {
        *status_ret = status_$invalid_line_number;
        return;
    }

    if (line >= TERM_$MAX_DTTE) {
        *status_ret = status_$requested_line_or_operation_not_implemented;
        return;
    }

    *status_ret = status_$ok;
    discipline = *discipline_ptr;
    dtte_offset = (short)(line * DTTE_SIZE);

    switch (discipline) {
        case 0:  // TTY mode
        case 3:  // SUMA mode
            handler_ptr = (void **)(TERM_$DTTE_BASE + dtte_offset + DTTE_OFFSET_12C8);
            if (*handler_ptr == NULL) {
                *status_ret = status_$invalid_line_number;
                return;
            }
            token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

            if (discipline == 0) {
                // Set up normal TTY handlers
                void **tty_struct = (void **)*handler_ptr;
                tty_struct[1] = *(void **)(TERM_$DTTE_BASE + dtte_offset + DTTE_OFFSET_12C4);
                tty_struct[10] = PTR_TTY_$I_RCV;                    // offset 0x28
                tty_struct[11] = PTR_TTY_$I_OUTPUT_BUFFER_DRAINED;  // offset 0x2c
                tty_struct[12] = PTR_TTY_$I_HUP;                    // offset 0x30
                tty_struct[13] = PTR_TTY_$I_INTERRUPT;              // offset 0x34
            } else {
                // SUMA mode - set receive handler
                void **tty_struct = (void **)*handler_ptr;
                tty_struct[10] = (void *)SUMA_$RCV;  // offset 0x28
            }
            ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
            break;

        case 1:  // Disable alternate handler
        case 2:  // Enable alternate handler
            alt_handler_ptr = (void **)(TERM_$DTTE_BASE + dtte_offset + DTTE_OFFSET_12CC);
            if (*alt_handler_ptr == NULL) {
                *status_ret = status_$invalid_line_number;
                return;
            }
            token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

            if (discipline == 1) {
                // Disable - clear the handler
                *(void **)*alt_handler_ptr = NULL;
            } else {
                // Enable alternate handler with font reload
                *(void **)*alt_handler_ptr = PTR_TTY_$I_RCV_ALT;
                ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
                DTTY_$RELOAD_FONT();
                TERM_$SEND_KBD_STRING(TERM_$KBD_STRING_DATA, TERM_$KBD_STRING_LEN);
                goto store_discipline;
            }
            ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
            break;

        default:
            // Unknown discipline - just store it
            break;
    }

store_discipline:
    // Store the discipline value in the DTTE
    *(short *)(TERM_$DTTE_BASE + dtte_offset + DTTE_OFFSET_12D4) = discipline;
}
