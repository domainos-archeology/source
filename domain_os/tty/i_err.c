/*
 * TTY_$I_ERR - Handle a receive error on a TTY line
 *
 * Installed as the line's error handler (TERM_$DATA + 0x28, 0x00E2CA18).
 * Asks the line's status handler what went wrong and either records the
 * error in the input stream (TTY_$I_STORE_PARITY, a nested procedure) or
 * flags the error and wakes both eventcounts.
 *
 * 0x00E1BE08..0x00E1BEA6 (160 bytes):
 *   0x00E1BE10  subq.l #2 / st -(SP) / move.l (A0) / jsr (0x2c0,A0)
 *               err = status_handler(line_id, true); D0 kept as a longword
 *   0x00E1BE26  D1w = byte (0xc,A6) zero-extended -- the second argument is
 *               a byte in the high half of its word slot
 *   0x00E1BE2C  ch == 0 and err == 0x360004:
 *                 input_flags bit 10 set -> store, else -> flag
 *   0x00E1BE48  otherwise:
 *                 (err == 0x36000b) and input_flags bit 10 -> store (the two
 *                 seq/sne booleans are and.b'ed and tested with bmi)
 *                 input_flags bit 11 clear -> flag
 *                 err == 0x360004 or err == 0x360005 -> store, else flag
 *   0x00E1BE78  bsr TTY_$I_STORE_PARITY (no arguments: it reads this frame's
 *               tty and ch through the static link)
 *   0x00E1BE7E  flag: bset.b #0,(0xb,A0) = pending_signal bit 0, then
 *               TTY_$I_ADVANCE_EC(input_ec), TTY_$I_ADVANCE_EC(output_ec)
 *
 * Status codes (stcode.db.10.2): 0x360004 "character framing error",
 * 0x360005 "character parity error", 0x36000b "DTR drop".  sio/sio.h names
 * 0x360004 status_$sio_parity_error and 0x360005 status_$sio_framing_error
 * (swapped relative to the database text); the numeric values are what the
 * image compares, so they are used here by value with the database text.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - the character received with the error (0 for a pure line error);
 *         TTY_$I_STORE_PARITY stores it after the 0xFF 0x00 marker
 *
 * Original address: 0x00e1be08
 * Size: 160 bytes
 */

#include "tty/tty_internal.h"

#define TTY_ERR_STATUS_FRAMING  0x00360004  /* "character framing error" */
#define TTY_ERR_STATUS_PARITY   0x00360005  /* "character parity error" */
#define TTY_ERR_STATUS_DTR_DROP 0x0036000b  /* "DTR drop" */

void TTY_$I_ERR(tty_desc_t *tty, uint8_t ch)
{
    status_$t err_status;                              /* (-0x4,A6) */
    boolean is_dtr_drop;                               /* D1b, 0x00E1BE4E seq */
    boolean ignore_dtr;                                /* D3b, 0x00E1BE5C sne */

    /* 0x00E1BE10..0x00E1BE22 */
    err_status = tty->status_handler(tty->line_id, true);

    if (ch == 0 && err_status == TTY_ERR_STATUS_FRAMING) {      /* 0x00E1BE2C */
        if ((tty->input_flags & 0x00000400) != 0) {             /* 0x00E1BE40 btst #10 */
            goto store;
        }
        goto flag;
    }

    /* 0x00E1BE48..0x00E1BE60 */
    is_dtr_drop = (err_status == TTY_ERR_STATUS_DTR_DROP) ? true : false;
    ignore_dtr = ((tty->input_flags & 0x00000400) != 0) ? true : false;
    if ((int8_t)(is_dtr_drop & ignore_dtr) < 0) {
        goto store;
    }
    if ((tty->input_flags & 0x00000800) == 0) {                 /* 0x00E1BE62 btst #11 */
        goto flag;
    }
    if (err_status == TTY_ERR_STATUS_FRAMING ||                 /* 0x00E1BE68 */
        err_status == TTY_ERR_STATUS_PARITY) {                  /* 0x00E1BE70 */
        goto store;
    }
    goto flag;

store:                                                          /* 0x00E1BE78 */
    TTY_$I_STORE_PARITY(tty, ch);
    return;

flag:                                                           /* 0x00E1BE7E */
    tty->pending_signal |= TTY_ERR_CALLBACK;
    TTY_$I_ADVANCE_EC(tty->input_ec);
    TTY_$I_ADVANCE_EC(tty->output_ec);
}
