/*
 * TTY kernel-level function character manipulation (re-verified 2026-09-22).
 * All four load A5 = 0x00E8242C and start with
 * tty = TTY_$I_GET_DESC(*line_ptr, status); status != 0 -> return.
 *
 * TTY_$K_SET_FUNC_CHAR, 0x00E674D2..0x00E67552 (130 bytes):
 *   0x00E67500  n = *func_ptr (word); n > 0x1F (bhi) or n > 0x11 (not bls)
 *               -> status 0x350002, return
 *   0x00E67518  func_enabled bit n (btst.l D1,D2) -> char_class[func_chars[n]] = 0x12
 *   0x00E67532  func_chars[n] = *ch_ptr
 *   0x00E6753A  raw_mode not negative (bmi skips) -> TTY_$I_SET_DFL_FUNCS(tty, true)
 *
 * TTY_$K_INQ_FUNC_CHAR, 0x00E67554..0x00E675AC (90 bytes):
 *   same range test; bad -> *ch_ptr = 0, status 0x350002; else
 *   *ch_ptr = func_chars[n]
 *
 * TTY_$K_ENABLE_FUNC, 0x00E675AE..0x00E67616 (106 bytes):
 *   *enable_ptr < 0 -> func_enabled |= bset.l(*func_ptr) (bit number mod 32)
 *   else func_enabled &= ~that; raw_mode not negative -> SET_DFL_FUNCS(tty, true)
 *
 * TTY_$K_INQ_FUNC_ENABLED, 0x00E67618..0x00E67654 (62 bytes):
 *   *enabled_ptr = func_enabled
 *
 * The earlier emission tested `tty->raw_mode >= 0` on a uint8_t, which is
 * always true; the image tests the sign bit (tst.b / bmi).
 */

#include "tty/tty_internal.h"

void TTY_$K_SET_FUNC_CHAR(short *line_ptr, const uint16_t *func_ptr,
                          const char *ch_ptr,
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

    /* 0x00E67502: cmpi.w #0x1f / bhi, then cmpi.w #0x11 / bls */
    if (func_num > 0x1f || func_num > 0x11) {
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

    /* tst.b (0x4d9,A2) / bmi: only when raw_mode's sign bit is clear */
    if ((int8_t)tty->raw_mode >= 0) {
        TTY_$I_SET_DFL_FUNCS(tty, true);
    }
}

void TTY_$K_INQ_FUNC_CHAR(short *line_ptr, const uint16_t *func_ptr, char *ch_ptr,
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

    /* 0x00E67588: the same two compares */
    if (func_num > 0x1f || func_num > 0x11) {
        *ch_ptr = 0;
        *status = status_$tty_invalid_function;
        return;
    }

    // Return the function character
    *ch_ptr = tty->func_chars[func_num];
}

void TTY_$K_ENABLE_FUNC(short *line_ptr, const uint16_t *func_ptr,
                        const char *enable_ptr,
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

    /* tst.b (0x4d9,A2) / bmi: only when raw_mode's sign bit is clear */
    if ((int8_t)tty->raw_mode >= 0) {
        TTY_$I_SET_DFL_FUNCS(tty, true);
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
