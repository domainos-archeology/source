/*
 * TTY_$I_SET_RAW_MODE - Switch a line between raw and cooked mode
 *
 * 0x00E1BF70..0x00E1C082 (276 bytes), A5 = 0x00E2DDB4 (the TTY data segment
 * "D E2DDB4 TTY size = 28": word_sep_bitmap at +0, DAT_00e2ddd4 at +0x20,
 * DAT_00e2ddd8 at +0x24), A2 = tty, raw = byte (0xc,A6).  Only caller:
 * TTY_$I_SET_RAW (0x00E673DA).
 *
 * raw < 0 (enter raw), 0x00E1BF8A..0x00E1C02C:
 *   raw_mode already negative -> return
 *   raw_saved_input_flags  = input_flags  & DAT_00e2ddd8; input_flags  &= ~mask
 *   raw_saved_output_flags = output_flags & DAT_00e2ddd4; output_flags &= ~mask
 *   TTY_$I_SET_DFL_FUNCS(tty, false)        (clr.w -(SP))
 *   crash_char != 0 -> char_class[crash_char] = 0x12
 *   move.l #0x10001,(0x38,A2): break_mode = 1, min_chars = 1
 *   spin lock; state_flags & 7 -> state_flags &= ~7, ADVANCE_EC(output_ec);
 *   spin unlock; raw_mode = 0xFF (st)
 * raw >= 0 (leave raw), 0x00E1C02E..0x00E1C076:
 *   raw_mode not negative -> return
 *   input_flags |= raw_saved_input_flags; output_flags |= raw_saved_output_flags
 *   both saved words cleared; break_mode = 0 (min_chars untouched)
 *   TTY_$I_SET_DFL_FUNCS(tty, true)         (st -(SP))
 *   crash_char != 0 -> char_class[crash_char] = 0x11
 *   raw_mode = 0
 *
 * Original address: 0x00e1bf70
 * Size: 276 bytes
 */

#include "tty/tty_internal.h"

void TTY_$I_SET_RAW_MODE(tty_desc_t *tty, char raw)
{
    ml_$spin_token_t token;                                /* (-0x6,A6) */

    if (raw < 0) {                                         /* 0x00E1BF82 bpl */
        if ((int8_t)tty->raw_mode < 0) {                   /* 0x00E1BF8A bmi */
            return;
        }

        tty->raw_saved_input_flags = tty->input_flags & DAT_00e2ddd8;     /* 0x00E1BF92 */
        tty->input_flags &= ~DAT_00e2ddd8;                                /* 0x00E1BF9E */
        tty->raw_saved_output_flags = tty->output_flags & DAT_00e2ddd4;   /* 0x00E1BFA8 */
        tty->output_flags &= ~DAT_00e2ddd4;                               /* 0x00E1BFB4 */

        TTY_$I_SET_DFL_FUNCS(tty, false);                  /* 0x00E1BFBE..0x00E1BFC4 */

        if (tty->crash_char != 0) {                        /* 0x00E1BFCC */
            tty->char_class[tty->crash_char] = TTY_CHAR_CLASS_NORMAL;
        }

        tty->break_mode = 1;                               /* 0x00E1BFE4 move.l #0x10001 */
        tty->min_chars = 1;

        token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);            /* 0x00E1BFEC */
        if ((tty->state_flags & 0x0007) != 0) {            /* 0x00E1BFFE */
            tty->state_flags &= 0xFFF8;                    /* 0x00E1C006 moveq #-8 */
            TTY_$I_ADVANCE_EC(tty->output_ec);             /* 0x00E1C00C */
        }
        ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);           /* 0x00E1C016 */

        tty->raw_mode = 0xFF;                              /* 0x00E1C028 st */
        return;
    }

    if ((int8_t)tty->raw_mode >= 0) {                      /* 0x00E1C02E bpl */
        return;
    }

    tty->input_flags |= tty->raw_saved_input_flags;        /* 0x00E1C034 */
    tty->output_flags |= tty->raw_saved_output_flags;      /* 0x00E1C03C */
    tty->raw_saved_input_flags = 0;                        /* 0x00E1C044 */
    tty->raw_saved_output_flags = 0;                       /* 0x00E1C048 */
    tty->break_mode = 0;                                   /* 0x00E1C04C clr.w */

    TTY_$I_SET_DFL_FUNCS(tty, true);                       /* 0x00E1C050..0x00E1C056 */

    if (tty->crash_char != 0) {                            /* 0x00E1C05E */
        tty->char_class[tty->crash_char] = TTY_CHAR_CLASS_CRASH;
    }

    tty->raw_mode = 0;                                     /* 0x00E1C076 */
}
