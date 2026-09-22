/*
 * TTY_$I_SET_RAW, TTY_$I_INQ_RAW, TTY_$I_ENABLE_CRASH_FUNC
 *
 * TTY_$I_SET_RAW, 0x00E673AA..0x00E673E8 (64 bytes), A5 = 0x00E8242C:
 *   line = word (0x8,A6), raw = byte in the high half of (0xa,A6),
 *   status = (0xc,A6).  tty = TTY_$I_GET_DESC(line, status) (A0 result);
 *   status != 0 -> return; TTY_$I_SET_RAW_MODE(tty, raw).
 *   Only caller: TERM_$CONTROL case 3 (0x00E669F0).
 *
 * TTY_$I_INQ_RAW, 0x00E673EA..0x00E67420 (56 bytes):
 *   line = word (0x8,A6), raw_ptr = (0xa,A6), status = (0xe,A6).
 *   tty = TTY_$I_GET_DESC(line, status); status != 0 -> return;
 *   *raw_ptr = tty->raw_mode (move.b (0x4d9,A0),(A1)).
 *   Only caller: TERM_$INQUIRE (0x00E66E7A).
 *
 * TTY_$I_ENABLE_CRASH_FUNC, 0x00E67292..0x00E672DC (76 bytes):
 *   tty = (0x8,A6), ch = byte (0xc,A6), enable = byte (0xe,A6).
 *   enable < 0: crash_char = ch; char_class[ch] = 0x11
 *   else:       crash_char != 0 -> char_class[ch] = 0x12 (indexed by the
 *               ARGUMENT, not by the old crash_char); crash_char = 0
 *   Callers: TERM_$INIT 0x00E32E6A, 0x00E331FA.
 */

#include "tty/tty_internal.h"

void TTY_$I_SET_RAW(short line, char raw, status_$t *status)
{
    tty_desc_t *tty;                                       /* A2 */

    tty = TTY_$I_GET_DESC(line, status);                   /* 0x00E673BC..0x00E673C4 */
    if (*status != status_$ok) {                           /* 0x00E673CE */
        return;
    }
    TTY_$I_SET_RAW_MODE(tty, raw);                         /* 0x00E673D2..0x00E673DA */
}

void TTY_$I_INQ_RAW(short line, char *raw, status_$t *status)
{
    tty_desc_t *tty;                                       /* A0 */

    tty = TTY_$I_GET_DESC(line, status);                   /* 0x00E673FC..0x00E67404 */
    if (*status != status_$ok) {                           /* 0x00E6740C */
        return;
    }
    *raw = (char)tty->raw_mode;                            /* 0x00E67414 */
}

void TTY_$I_ENABLE_CRASH_FUNC(tty_desc_t *tty, uint8_t ch, char enable)
{
    if (enable < 0) {                                      /* 0x00E672A4 bpl */
        tty->crash_char = ch;                              /* 0x00E672A6 */
        tty->char_class[ch] = TTY_CHAR_CLASS_CRASH;        /* 0x00E672B4 */
        return;
    }
    if (tty->crash_char != 0) {                            /* 0x00E672BC */
        tty->char_class[ch] = TTY_CHAR_CLASS_NORMAL;       /* 0x00E672CC */
    }
    tty->crash_char = 0;                                   /* 0x00E672D2 */
}
