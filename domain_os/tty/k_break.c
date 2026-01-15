// TTY kernel-level input break mode functions
//
// TTY_$K_SET_INPUT_BREAK_MODE - Set input break mode
// Address: 0x00e6781e
//
// TTY_$K_INQ_INPUT_BREAK_MODE - Inquire input break mode
// Address: 0x00e678bc

#include "tty/tty_internal.h"

// Break mode structure (8 bytes)
typedef struct {
    uint16_t mode;        // 0: raw mode, 1-3: various line modes
    uint16_t min_chars;   // Minimum characters before break
    uint32_t reserved;    // Reserved
} break_mode_t;

void TTY_$K_SET_INPUT_BREAK_MODE(short *line_ptr, void *mode_ptr, status_$t *status)
{
    tty_desc_t *tty;
    break_mode_t *mode;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    mode = (break_mode_t *)mode_ptr;

    // Copy the break mode structure
    tty->break_mode = mode->mode;
    tty->min_chars = mode->min_chars;
    tty->reserved_3C = mode->reserved;

    if (mode->mode == 0) {
        // Raw mode: enable break character processing
        FUN_00e6720e(tty, DAT_00e82454, true);

        // If crash char is set, make it trigger crash
        if (tty->crash_char != 0) {
            tty->char_class[tty->crash_char] = TTY_CHAR_CLASS_CRASH;
        }
    } else {
        // Line mode: disable break character processing
        FUN_00e6720e(tty, DAT_00e82454, false);

        // If crash char is set, make it a normal character
        if (tty->crash_char != 0) {
            tty->char_class[tty->crash_char] = TTY_CHAR_CLASS_NORMAL;
        }
    }
}

void TTY_$K_INQ_INPUT_BREAK_MODE(short *line_ptr, void *mode_ptr, status_$t *status)
{
    tty_desc_t *tty;
    break_mode_t *mode;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    mode = (break_mode_t *)mode_ptr;

    // Return the current break mode settings
    mode->mode = tty->break_mode;
    mode->min_chars = tty->min_chars;
    mode->reserved = tty->reserved_3C;
}
