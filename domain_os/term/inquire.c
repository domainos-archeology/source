/*
 * TERM_$INQUIRE - Terminal attribute inquiry
 *
 * A 33-way jump on *option_ptr (0..32; `cmpi.w #0x21 / bcc` sends anything
 * else to the invalid-option arm), each arm filling *value_ret from one of
 * the TTY_$K_INQ_* / TTY_$I_INQ_RAW / SIO_$K_INQ_PARAM / DTTE sources.
 * Every arm but option 7's error exit ends in TERM_$STATUS_CONVERT.
 *
 * Parameters (frame 0x00E66D98..0x00E66DA0):
 *   0x08 line_ptr   - word by reference (A2)
 *   0x0C option_ptr - word by reference
 *   0x10 value_ret  - receives a byte, a word, or an 8-byte uid_t
 *   0x14 status_ret - status return (A3)
 *
 * Original address: 0x00e66d90, 986 bytes
 *
 * Jump table at 0x00E66DB8 (`gsk read 0xE66DB8 66`), option -> arm:
 *    0 E66DFA FUNC_CHAR &word0 (0xE66898)      17 E670AC PARAM 0xE671BC, flags2 bit 1
 *    1 E66E06 FUNC_CHAR &word2 (0xE667C4)      18 E670F2 PARAM 0xE671B4, parity 3/1/0
 *    2 E66E12 FUNC_CHAR &word3 (0xE66D82)      19 E66FCE PARAM 0xE671D8, char_size
 *    3 E66E70 TTY_$I_INQ_RAW(*line, value)     20 E66FF0 PARAM 0xE671D4, stop_bits
 *    4 E66E88 INPUT_FLAGS bit 0 -> seq         21 E6713C PARAM 0xE671B0, break_mask bits
 *    5 E66E50 OUTPUT_FLAGS bit 1 -> sne        22 E67194 invalid option
 *    6 E66FAC PARAM 0xE671AC, baud low word    23 E66E1E FUNC_CHAR &word8 (0xE66D8E)
 *    7 E66EA8 dtte[real line].flags byte       24 E66FA2 byte 0
 *    8 E66EDA FUNC_ENABLED bits 13 & 14        25 E66F36 FUNC_ENABLED bit 9
 *    9 E67134 word 0                           26 E66E2A FUNC_CHAR &word9 (0xE66D8C)
 *   10 E66F1A FUNC_ENABLED bit 8               27 E66F52 FUNC_ENABLED bit 10
 *   11 E66F00 INPUT_FLAGS bit 1 -> sne         28 E66E36 FUNC_CHAR &word10 (0xE66D8A)
 *   12 E67012 PARAM 0xE671D0, flags1 bit 0     29 E66E50 (= option 5)
 *   13 E67032 PARAM 0xE671CC, flags1 bit 3     30 E66F78 PGROUP, 8 bytes
 *   14 E67070 PARAM 0xE671C4, flags1 bit 2     31 E670CA PARAM 0xE671B8, flags2 bit 0
 *   15 E6708E PARAM 0xE671C0, flags2 bit 2     32 E66FAC (= option 6)
 *   16 E67052 PARAM 0xE671C8, flags1 bit 1
 *
 * Frame: -0x18 sio_params_t (0x16 bytes; the `btst.b` byte offsets -0x15,
 * -0x11, -0x0D are the low bytes of flags1, flags2, break_mask), -0x20
 * pgroup uid, -0x24/-0x28/-0x2C flag longwords, -0x40 the flow-control word.
 */

#include "term/term_internal.h"

/*
 * Function-number words in the code region, `gsk read 0xE66D82 14`:
 * 00 03 | 00 0e | 00 0d | ff 00 | 00 0a | 00 09 | 00 08.  Word 0 and word 2
 * are the shared cells term_$const_word_0 / term_$const_word_2.
 */
static const uint16_t term_$c_func_3  = 3;   /* 0x00E66D82 */
static const uint16_t term_$c_func_10 = 10;  /* 0x00E66D8A - STATUS */
static const uint16_t term_$c_func_9  = 9;   /* 0x00E66D8C - DSUSP */
static const uint16_t term_$c_func_8  = 8;   /* 0x00E66D8E - SUSP */

/*
 * SIO_$K_INQ_PARAM selector longwords, `gsk read 0xE671AC 48`, one per
 * `pea (d,PC)` in the code region after this routine.
 */
static const uint32_t sio_sel_speed      = 0x00000001; /* 0x00E671AC */
static const uint32_t sio_sel_flow_ctrl  = 0x00002000; /* 0x00E671B0 */
static const uint32_t sio_sel_parity     = 0x00000004; /* 0x00E671B4 */
static const uint32_t sio_sel_flags2_b0  = 0x00000200; /* 0x00E671B8 */
static const uint32_t sio_sel_flags2_b1  = 0x00000400; /* 0x00E671BC */
static const uint32_t sio_sel_flags2_b2  = 0x00000800; /* 0x00E671C0 */
static const uint32_t sio_sel_flags1_b2  = 0x00000080; /* 0x00E671C4 */
static const uint32_t sio_sel_flags1_b1  = 0x00000100; /* 0x00E671C8 */
static const uint32_t sio_sel_flags1_b3  = 0x00000040; /* 0x00E671CC */
static const uint32_t sio_sel_flags1_b0  = 0x00000020; /* 0x00E671D0 */
static const uint32_t sio_sel_stop_bits  = 0x00000008; /* 0x00E671D4 */
static const uint32_t sio_sel_char_size  = 0x00000010; /* 0x00E671D8 */

#define BOOL_BYTE(cond) ((unsigned char)((cond) ? 0xFF : 0x00))   /* sne D1b */

void TERM_$INQUIRE(short *line_ptr, unsigned short *option_ptr,
                   unsigned short *value_ret, status_$t *status_ret)
{
    uint16_t option;            /* D0w at 0x00E66DA4 */
    int16_t real_line;          /* D0w at 0x00E66EB4 */
    uint32_t oflags;            /* A6-0x24 */
    uint32_t iflags;            /* A6-0x28 */
    uint32_t enabled;           /* A6-0x2C */
    sio_params_t params;        /* A6-0x18 */
    uid_t pgroup;               /* A6-0x20 */
    uint16_t flow;              /* A6-0x40 */

    /* 0x00E66DA0..0x00E66DB4 */
    option = *option_ptr;
    if (option >= 0x21) {
        option = 22;            /* the invalid-option arm, see below */
    }

    switch (option) {
    case 0:
        TTY_$K_INQ_FUNC_CHAR(line_ptr, &term_$const_word_0, (char *)value_ret, status_ret);
        break;
    case 1:
        TTY_$K_INQ_FUNC_CHAR(line_ptr, &term_$const_word_2, (char *)value_ret, status_ret);
        break;
    case 2:
        TTY_$K_INQ_FUNC_CHAR(line_ptr, &term_$c_func_3, (char *)value_ret, status_ret);
        break;
    case 23:
        TTY_$K_INQ_FUNC_CHAR(line_ptr, &term_$c_func_8, (char *)value_ret, status_ret);
        break;
    case 26:
        TTY_$K_INQ_FUNC_CHAR(line_ptr, &term_$c_func_9, (char *)value_ret, status_ret);
        break;
    case 28:
        TTY_$K_INQ_FUNC_CHAR(line_ptr, &term_$c_func_10, (char *)value_ret, status_ret);
        break;

    case 3:
        /* 0x00E66E70..0x00E66E80: *line by value, value_ret passed straight through */
        TTY_$I_INQ_RAW(*line_ptr, (char *)value_ret, status_ret);
        break;

    case 4:
        /* 0x00E66E88..0x00E66EA4: btst #0 / seq */
        TTY_$K_INQ_INPUT_FLAGS(line_ptr, &iflags, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((iflags & 0x00000001u) == 0);
        break;
    case 11:
        /* 0x00E66F00..0x00E66F16 -> 0x00E66E68: btst #1 / sne */
        TTY_$K_INQ_INPUT_FLAGS(line_ptr, &iflags, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((iflags & 0x00000002u) != 0);
        break;

    case 5:
    case 29:
        /* 0x00E66E50..0x00E66E6C: btst #1 / sne */
        TTY_$K_INQ_OUTPUT_FLAGS(line_ptr, &oflags, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((oflags & 0x00000002u) != 0);
        break;

    case 6:
    case 32:
        /* 0x00E66FAC..0x00E66FC6: move.w (-0xa,A6) = low word of baud_rate */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_speed, status_ret);
        *value_ret = (uint16_t)params.baud_rate;
        break;

    case 7:
        /* 0x00E66EA8..0x00E66ED2; the error exit skips STATUS_CONVERT */
        real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
        *(unsigned char *)value_ret = DTTE[real_line].flags;
        break;

    case 8:
        /* 0x00E66EDA..0x00E66EFC: sne of bit 13 AND sne of bit 14 */
        TTY_$K_INQ_FUNC_ENABLED(line_ptr, &enabled, status_ret);
        *(unsigned char *)value_ret = (unsigned char)
            (BOOL_BYTE((enabled & 0x00002000u) != 0) & BOOL_BYTE((enabled & 0x00004000u) != 0));
        break;
    case 10:
        /* 0x00E66F1A: btst #8 */
        TTY_$K_INQ_FUNC_ENABLED(line_ptr, &enabled, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((enabled & 0x00000100u) != 0);
        break;
    case 25:
        /* 0x00E66F36: btst #9 */
        TTY_$K_INQ_FUNC_ENABLED(line_ptr, &enabled, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((enabled & 0x00000200u) != 0);
        break;
    case 27:
        /* 0x00E66F52: btst #10 */
        TTY_$K_INQ_FUNC_ENABLED(line_ptr, &enabled, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((enabled & 0x00000400u) != 0);
        break;

    case 9:
        /* 0x00E67134: clr.w (A0) */
        *value_ret = 0;
        break;
    case 24:
        /* 0x00E66FA2: clr.b (A0) */
        *(unsigned char *)value_ret = 0;
        break;

    case 12:
        /* 0x00E67012: btst.b #0,(-0x15,A6) = flags1 bit 0 */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags1_b0, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((params.flags1 & 0x00000001u) != 0);
        break;
    case 13:
        /* 0x00E67032: flags1 bit 3 */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags1_b3, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((params.flags1 & 0x00000008u) != 0);
        break;
    case 14:
        /* 0x00E67070: flags1 bit 2 */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags1_b2, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((params.flags1 & 0x00000004u) != 0);
        break;
    case 16:
        /* 0x00E67052: flags1 bit 1 */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags1_b1, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((params.flags1 & 0x00000002u) != 0);
        break;
    case 15:
        /* 0x00E6708E: btst.b #2,(-0x11,A6) = flags2 bit 2 */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags2_b2, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((params.flags2 & 0x00000004u) != 0);
        break;
    case 17:
        /* 0x00E670AC: flags2 bit 1 */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags2_b1, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((params.flags2 & 0x00000002u) != 0);
        break;
    case 31:
        /* 0x00E670CA: flags2 bit 0 */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags2_b0, status_ret);
        *(unsigned char *)value_ret = BOOL_BYTE((params.flags2 & 0x00000001u) != 0);
        break;

    case 18:
        /* 0x00E670F2..0x00E6713A: parity 3 -> 3, 1 -> 1, 0 -> 0, else untouched */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_parity, status_ret);
        if (params.parity == 3) {
            *value_ret = 3;
        } else if (params.parity == 1) {
            *value_ret = 1;
        } else if (params.parity == 0) {
            *value_ret = 0;
        }
        break;
    case 19:
        /* 0x00E66FCE..0x00E66FE8: move.w (-0x8,A6) = char_size */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_char_size, status_ret);
        *value_ret = (uint16_t)params.char_size;
        break;
    case 20:
        /* 0x00E66FF0..0x00E6700A: move.w (-0x6,A6) = stop_bits */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_stop_bits, status_ret);
        *value_ret = (uint16_t)params.stop_bits;
        break;

    case 21:
        /*
         * 0x00E6713C..0x00E6718E: break_mask bits 0, 1, 3, 4 (the low byte
         * at -0xD) are gathered by `bset.b` into bits 0..3 of the byte at
         * -0x3F, and the WORD at -0x40 is stored.  QUIRK: nothing clears
         * that word first, so in the image its other twelve bits are stack
         * residue; the C starts from zero.
         */
        SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flow_ctrl, status_ret);
        flow = 0;
        if (params.break_mask & 0x00000001u) flow |= 0x0001;
        if (params.break_mask & 0x00000002u) flow |= 0x0002;
        if (params.break_mask & 0x00000008u) flow |= 0x0004;
        if (params.break_mask & 0x00000010u) flow |= 0x0008;
        *value_ret = flow;
        break;

    case 30:
        /* 0x00E66F78..0x00E66F9A: two longword moves into *value_ret */
        TTY_$K_INQ_PGROUP(line_ptr, &pgroup, status_ret);
        ((uid_t *)value_ret)->high = pgroup.high;
        ((uid_t *)value_ret)->low = pgroup.low;
        break;

    case 22:
    default:
        /* 0x00E67194 */
        *status_ret = status_$term_invalid_option;
        break;
    }

    /* 0x00E6719A..0x00E6719C: args reclaimed by unlk */
    TERM_$STATUS_CONVERT(status_ret);
}
