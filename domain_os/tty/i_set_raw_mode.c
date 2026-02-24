/*
 * TTY_$I_SET_RAW_MODE - Switch TTY between raw and cooked modes
 *
 * When entering raw mode (param_2 < 0, i.e. true in Domain/OS):
 *   - Saves current input_flags and output_flags that will be
 *     cleared, into reserved_18/reserved_10
 *   - Clears the relevant bits from input_flags and output_flags
 *   - Resets function character classes to NORMAL (via TTY_$I_SET_DFL_FUNCS)
 *   - If a crash character is defined, sets its class to NORMAL
 *   - Sets break_mode to 1, min_chars to 1 (char-at-a-time)
 *   - If output is stopped (state_flags bits 0-2), clears those bits
 *     and advances the output eventcount
 *   - Sets raw_mode flag to 0xFF
 *
 * When leaving raw mode (param_2 >= 0):
 *   - Restores saved input_flags and output_flags
 *   - Clears the saved copies
 *   - Resets break_mode to 0 (line mode)
 *   - Restores function character classes to defaults
 *   - If crash character defined, sets its class back to CRASH (0x11)
 *   - Clears raw_mode flag
 *
 * The masks used for saving/restoring flags:
 *   DAT_00e2ddd4 = 0x0000001F (output_flags mask)
 *   DAT_00e2ddd8 = 0x0000003C (input_flags mask)
 *
 * Parameters:
 *   tty - TTY descriptor
 *   raw - Raw mode flag (negative = enter raw, non-negative = leave raw)
 *
 * Original address: 0x00e1bf70
 * Size: 276 bytes
 */

#include "tty/tty_internal.h"

/* DAT_00e2ddd4 and DAT_00e2ddd8 declared in tty_internal.h */

void TTY_$I_SET_RAW_MODE(tty_desc_t *tty, char raw)
{
    ml_$spin_token_t token;

    if (raw < 0) {
        /* Enter raw mode */

        /* Only transition if not already in raw mode */
        if ((int8_t)tty->raw_mode < 0) {
            return;
        }

        /* Save and clear input_flags bits specified by mask
         * Note: writes uint32_t at offset 0x18, spanning reserved_18 + reserved_1A */
        *(uint32_t *)((char *)tty + 0x18) = tty->input_flags & DAT_00e2ddd8;
        tty->input_flags = ~DAT_00e2ddd8 & tty->input_flags;

        /* Save and clear output_flags bits specified by mask */
        tty->reserved_10 = tty->output_flags & DAT_00e2ddd4;
        tty->output_flags &= ~DAT_00e2ddd4;

        /* Reset all function char classes to NORMAL (not using defaults) */
        TTY_$I_SET_DFL_FUNCS(tty, 0);

        /* If crash character is defined, set its class to NORMAL */
        if (tty->crash_char != 0) {
            tty->char_class[(uint8_t)tty->crash_char] = TTY_CHAR_CLASS_NORMAL;
        }

        /* Set break_mode=1 (char-at-a-time), min_chars=1 */
        tty->break_mode = 1;
        tty->min_chars = 1;

        /* If output is stopped, restart it */
        token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);
        if ((tty->state_flags & 0x07) != 0) {
            tty->state_flags &= 0xFFF8;
            TTY_$I_ADVANCE_EC(tty->output_ec);
        }
        ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);

        /* Mark as raw mode */
        tty->raw_mode = 0xFF;
    } else {
        /* Leave raw mode */

        /* Only transition if currently in raw mode */
        if ((int8_t)tty->raw_mode >= 0) {
            return;
        }

        /* Restore saved input_flags */
        tty->input_flags = *(uint32_t *)((char *)tty + 0x18) | tty->input_flags;

        /* Restore saved output_flags */
        tty->output_flags |= tty->reserved_10;

        /* Clear saved copies */
        *(uint32_t *)((char *)tty + 0x18) = 0;
        tty->reserved_10 = 0;

        /* Reset break_mode to 0 (line mode) */
        tty->break_mode = 0;

        /* Restore function char classes to defaults */
        TTY_$I_SET_DFL_FUNCS(tty, 0xFF);

        /* If crash character is defined, set its class back to CRASH */
        if (tty->crash_char != 0) {
            tty->char_class[(uint8_t)tty->crash_char] = TTY_CHAR_CLASS_CRASH;
        }

        /* Clear raw mode flag */
        tty->raw_mode = 0;
    }
}
