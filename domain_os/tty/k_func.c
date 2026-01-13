// TTY kernel-level function character manipulation
//
// TTY_$K_SET_FUNC_CHAR - Set function character binding
// Address: 0x00e674d2
//
// TTY_$K_INQ_FUNC_CHAR - Inquire function character binding
// Address: 0x00e67554
//
// TTY_$K_ENABLE_FUNC - Enable/disable function character
// Address: 0x00e675ae
//
// TTY_$K_INQ_FUNC_ENABLED - Inquire enabled function characters
// Address: 0x00e67618

#include "tty.h"

// External helper to rebuild character class table
extern void FUN_00e6726e(tty_desc_t *tty, char update_all);

void TTY_$K_SET_FUNC_CHAR(short *line_ptr, ushort *func_ptr, char *ch_ptr,
                          status_$t *status)
{
    tty_desc_t *tty;
    ushort func_num;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    func_num = *func_ptr;

    // Validate function number (must be < 0x12)
    if (func_num >= 0x20 || func_num >= TTY_MAX_FUNC_CHARS) {
        *status = status_$tty_invalid_function;
        return;
    }

    // If this function was enabled, clear the old character's class
    if ((tty->func_enabled & (1U << func_num)) != 0) {
        uint8_t old_char = tty->func_chars[func_num];
        tty->char_class[old_char] = TTY_CHAR_CLASS_NORMAL;
    }

    // Set the new function character
    tty->func_chars[func_num] = *ch_ptr;

    // Rebuild character class table if not in raw mode
    if (tty->raw_mode >= 0) {  // Not in raw mode
        FUN_00e6726e(tty, true);
    }
}

void TTY_$K_INQ_FUNC_CHAR(short *line_ptr, ushort *func_ptr, char *ch_ptr,
                          status_$t *status)
{
    tty_desc_t *tty;
    ushort func_num;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    func_num = *func_ptr;

    // Validate function number
    if (func_num >= 0x20 || func_num >= TTY_MAX_FUNC_CHARS) {
        *ch_ptr = 0;
        *status = status_$tty_invalid_function;
        return;
    }

    // Return the function character
    *ch_ptr = tty->func_chars[func_num];
}

void TTY_$K_ENABLE_FUNC(short *line_ptr, ushort *func_ptr, char *enable_ptr,
                        status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Set or clear the function enabled bit
    if (*enable_ptr < 0) {  // true
        tty->func_enabled |= (1U << (*func_ptr & 0x1F));
    } else {
        tty->func_enabled &= ~(1U << (*func_ptr & 0x1F));
    }

    // Rebuild character class table if not in raw mode
    if (tty->raw_mode >= 0) {  // Not in raw mode
        FUN_00e6726e(tty, true);
    }
}

void TTY_$K_INQ_FUNC_ENABLED(short *line_ptr, uint32_t *enabled_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    *enabled_ptr = tty->func_enabled;
}
