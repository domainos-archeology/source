/*
 * TTY_$I_PUT_OUTPUT - Write to the output ring unless typed-ahead input
 * must be drained first
 *
 * 0x00E1BF0E..0x00E1BF6E (98 bytes), A2 = tty.  Only caller: TTY_$K_PUT
 * (0x00E67C74).  Arguments: tty (0x8), buf (0xc), count word (0x10),
 * reserve word (0x12).
 *   0x00E1BF18  input_flags bit 6 (btst.b #6,(0x17,A2)) set and
 *               input_tail != input_head:
 *                 spin lock; bset.b #1,(0x9,A2) (state INPUT_WAIT);
 *                 spin unlock; result 0
 *   0x00E1BF56  else result = tty_$i_put_chars(tty, buf, count:reserve)
 *               (the two words pushed in order form the callee's longword)
 *
 * Returns: characters accepted (D0w), or 0 when deferred.
 *
 * Original address: 0x00e1bf0e
 * Size: 98 bytes
 */

#include "tty/tty_internal.h"

int16_t TTY_$I_PUT_OUTPUT(tty_desc_t *tty, void *buf, uint16_t count, uint16_t max)
{
    ml_$spin_token_t token;                                /* (-0x6,A6) */

    if ((tty->input_flags & 0x00000040) != 0 &&            /* 0x00E1BF18 */
        tty->input_tail != tty->input_head) {              /* 0x00E1BF20 */
        token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);            /* 0x00E1BF2A */
        tty->state_flags |= TTY_STATUS_INPUT_WAIT;         /* 0x00E1BF3C */
        ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);           /* 0x00E1BF42 */
        return 0;                                          /* 0x00E1BF52 */
    }

    return (int16_t)tty_$i_put_chars(tty, (const uint8_t *)buf,
                                     ((uint32_t)count << 16) | (uint32_t)max);   /* 0x00E1BF56 */
}
