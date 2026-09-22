/*
 * TTY kernel-level input break mode functions (re-verified 2026-09-22).
 * Both load A5 = 0x00E8242C (the TTY data segment) and are reached through
 * the syscall table at 0x00E7B836 / 0x00E7B83A.
 *
 * TTY_$K_SET_INPUT_BREAK_MODE, 0x00E6781E..0x00E678BA (158 bytes):
 *   0x00E67834  tty = TTY_$I_GET_DESC(*line_ptr, status); status != 0 -> exit
 *   0x00E6784E  two longwords from the caller's record -> (0x38,A3), (0x3c,A3)
 *               i.e. break_mode, min_chars, reserved_3C
 *   0x00E67858  record word 0 == 0:
 *                 tty_$i_set_funcs(tty, DAT_00e82454 (0x28,A5), true (st));
 *                 crash_char != 0 -> char_class[crash_char] = 0x11
 *               else:
 *                 tty_$i_set_funcs(tty, DAT_00e82454, false (clr.w));
 *                 crash_char != 0 -> char_class[crash_char] = 0x12
 *
 * TTY_$K_INQ_INPUT_BREAK_MODE, 0x00E678BC..0x00E678FE (68 bytes):
 *   tty = GET_DESC(*line_ptr, status); status != 0 -> exit;
 *   two longwords from (0x38,A0) -> the caller's record (0xc,A6)
 */

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
        tty_$i_set_funcs(tty, DAT_00e82454, true);

        // If crash char is set, make it trigger crash
        if (tty->crash_char != 0) {
            tty->char_class[tty->crash_char] = TTY_CHAR_CLASS_CRASH;
        }
    } else {
        // Line mode: disable break character processing
        tty_$i_set_funcs(tty, DAT_00e82454, false);

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
