/*
 * TTY_$I_INIT - Initialize a TTY descriptor structure
 *
 * 0x00E3324C..0x00E33360 (278 bytes; map "I E3324C TTY size = 118", the
 * only routine in that segment).  A5 = 0x00E351B0, the module data segment
 * "D E351B0 TTY size = 3C", laid out as tty_$i_init_data below.  Only
 * caller: TERM_$INIT (0x00E32B62).
 *
 *   0x00E3325E  state_flags = 0
 *   0x00E33262  input_flags = 0x29; output_flags = 2; echo_flags = 0x23
 *   0x00E33274  raw_saved_input_flags = 0; raw_saved_output_flags = 0;
 *               crash_char = 0
 *   0x00E33280  dbf #0x11: func_chars[0..17] = (0x28,A5)[0..17]
 *   0x00E33290  func_enabled = 0x1FFEF
 *   0x00E33298  dbf #0xFF: char_class[0..255] = 0x12
 *   0x00E332B6  break_mode = 0
 *   0x00E332BA  pgroup_uid = UID_$NIL (two longwords from 0x00E1737C)
 *   0x00E332C8  session_id = 0; column = 0; saved_input_flags = 0;
 *               pending_signal = 0
 *   0x00E332D8  TTY_$I_SET_DFL_FUNCS(tty, true)   (result slot + st)
 *   0x00E332EA  raw_mode = 0
 *   0x00E332EE  dbf #5: signals[i].tty_desc = tty;
 *               signals[i].fault_status = (A5)[i] (longwords 0x00..0x17);
 *               signals[i].signal_num = (0x18,A5)[i] (words 0x18..0x23)
 *   0x00E3332E  input_head = input_read = input_tail = 1; input_size = 0x100
 *   0x00E33346  output_head = output_read = 1; output_tail = 0x100
 *
 * The earlier emission dropped the TTY_$I_SET_DFL_FUNCS call and filled
 * the two tables with invented values; both are now the image's bytes.
 *
 * Original address: 0x00e3324c
 * Size: 278 bytes
 */

#include "tty/tty_internal.h"

/*
 * The module data segment at 0x00E351B0 (0x3C bytes), `gsk read 0xE351B0`:
 *   00 12 00 10  00 12 00 1f  00 12 00 28  00 0b 00 0e  00 00 00 00  00 00 00 00
 *   00 03  00 02  00 15  00 01  00 1a  00 16   00 00 00 00
 *   08 17 15 04 00 09 0d 0a 1c 03 1a 19 16 13 11 0f 12 00
 *   00 00
 * The six longwords are what TTY_$I_INIT copies into signals[i].fault_status:
 * they are fault status codes (stcode.db.10.2: 0x120010 "process quit",
 * 0x12001F "process interrupt", 0x120028 "process suspend from keyboard",
 * 0xB000E "hangup fault"), paired with signal numbers 3, 2, 0x15, 1, 0x1A,
 * 0x16.
 */
typedef struct tty_$i_init_data_t {
    uint32_t signal_status[6];                 /* 0x00: per-signal fault status */
    uint16_t signal_num[6];                    /* 0x18: per-signal number */
    uint32_t pad_24;                           /* 0x24: zero in the image */
    uint8_t  func_chars[TTY_MAX_FUNC_CHARS];   /* 0x28: default function chars */
    uint16_t pad_3a;                           /* 0x3A */
} tty_$i_init_data_t;

_Static_assert(__builtin_offsetof(tty_$i_init_data_t, signal_num) == 0x18, "tty_$i_init_data_t.signal_num");
_Static_assert(__builtin_offsetof(tty_$i_init_data_t, func_chars) == 0x28, "tty_$i_init_data_t.func_chars");
_Static_assert(sizeof(tty_$i_init_data_t) == 0x3C, "tty_$i_init_data_t: D E351B0 TTY size = 3C");

static const tty_$i_init_data_t tty_$i_init_data = {
    .signal_status = {
        0x00120010,     /* [0] "process quit"                  sig 3  */
        0x0012001F,     /* [1] "process interrupt"             sig 2  */
        0x00120028,     /* [2] "process suspend from keyboard" sig 0x15 */
        0x000B000E,     /* [3] "hangup fault"                  sig 1  */
        0x00000000,     /* [4]                                 sig 0x1A */
        0x00000000,     /* [5]                                 sig 0x16 */
    },
    .signal_num = { 0x0003, 0x0002, 0x0015, 0x0001, 0x001A, 0x0016 },
    .pad_24 = 0,
    .func_chars = {
        0x08,   /*  0: BS   */
        0x17,   /*  1: ^W   */
        0x15,   /*  2: ^U   */
        0x04,   /*  3: ^D   */
        0x00,   /*  4: NUL  */
        0x09,   /*  5: TAB  */
        0x0d,   /*  6: CR   */
        0x0a,   /*  7: LF   */
        0x1c,   /*  8: ^\   */
        0x03,   /*  9: ^C   */
        0x1a,   /* 10: ^Z   */
        0x19,   /* 11: ^Y   */
        0x16,   /* 12: ^V   */
        0x13,   /* 13: ^S   */
        0x11,   /* 14: ^Q   */
        0x0f,   /* 15: ^O   */
        0x12,   /* 16: ^R   */
        0x00,   /* 17: NUL  */
    },
    .pad_3a = 0,
};

void TTY_$I_INIT(tty_desc_t *tty)
{
    int16_t i;

    tty->state_flags = 0;                                  /* 0x00E3325E */
    tty->input_flags = 0x29;                               /* 0x00E33262 */
    tty->output_flags = 2;                                 /* 0x00E33268 */
    tty->echo_flags = 0x23;                                /* 0x00E3326E */
    tty->raw_saved_input_flags = 0;                        /* 0x00E33274 clr.l (0x18) */
    tty->raw_saved_output_flags = 0;                       /* 0x00E33278 clr.l (0x10) */
    tty->crash_char = 0;                                   /* 0x00E3327C */

    /* 0x00E33280: moveq #0x11 + dbf = 18 iterations */
    for (i = 0; i < TTY_MAX_FUNC_CHARS; i++) {
        tty->func_chars[i] = tty_$i_init_data.func_chars[i];
    }

    tty->func_enabled = 0x0001FFEF;                        /* 0x00E33290 */

    /* 0x00E33298: move.w #0xff + dbf = 256 iterations, index byte D1b */
    for (i = 0; i < 256; i++) {
        tty->char_class[i] = TTY_CHAR_CLASS_NORMAL;
    }

    tty->break_mode = 0;                                   /* 0x00E332B6 */
    tty->pgroup_uid.high = UID_$NIL.high;                  /* 0x00E332BA..0x00E332C4 */
    tty->pgroup_uid.low = UID_$NIL.low;
    tty->session_id = 0;                                   /* 0x00E332C8 */
    tty->column = 0;                                       /* 0x00E332CC */
    tty->saved_input_flags = 0;                            /* 0x00E332D0 */
    tty->pending_signal = 0;                               /* 0x00E332D4 */

    TTY_$I_SET_DFL_FUNCS(tty, true);                       /* 0x00E332D8..0x00E332DE */

    tty->raw_mode = 0;                                     /* 0x00E332EA */

    /* 0x00E332EE: moveq #5 + dbf = 6 entries */
    for (i = 0; i < 6; i++) {
        tty->signals[i].tty_desc = ARCH_PTR_TO_VA(tty);                 /* 0x00E33302 */
        tty->signals[i].fault_status = (status_$t)tty_$i_init_data.signal_status[i];  /* 0x00E3330A */
        tty->signals[i].signal_num = tty_$i_init_data.signal_num[i];   /* 0x00E3330E */
    }

    tty->input_head = 1;                                   /* 0x00E3332E */
    tty->input_read = 1;
    tty->input_tail = 1;
    tty->input_size = 0x100;
    tty->output_head = 1;                                  /* 0x00E33346 */
    tty->output_read = 1;
    tty->output_tail = 0x100;
}
