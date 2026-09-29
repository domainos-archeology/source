#include "term/term_internal.h"

/*
 * TERM_$CONTROL - terminal control entry point (0x00E66916..0x00E66D80,
 * map: "I E66738 OS_TERM size = AA4", entry at 0x00E66916, 1050 bytes).
 *
 * Frame: link.w A6,-0x38; A2 = line_ptr (0x8,A6), A3 = status_ret (0x14,A6),
 * value_ptr = (0x10,A6).  *option_ptr (0xC,A6) is compared against 0x25 and
 * dispatched through the 37-entry jump table at 0x00E6693E (0x00E6692C..
 * 0x00E6693A); >= 37 goes to 0x00E66D68 (invalid option, then convert).
 *
 * Exits.  There are exactly two:
 *   0x00E66D70  pea (A3); jsr TERM_$STATUS_CONVERT  -- every arm except:
 *   0x00E66D78  movem/unlk/rts with NO conversion   -- case 7 when
 *               TERM_$GET_REAL_LINE fails (0x00E66B3C), and the "bad value"
 *               paths of cases 18/19/20 (0x00E66CEA).
 * The earlier emission skipped the conversion for cases 3, 7, 9, 24, 33, 34,
 * 35 and for the invalid-option default; all of those reach 0x00E66D70.
 *
 * TTY function / flag numbers.  The Pascal compiler placed these literals in
 * the code region and passes them by reference (`pea (d,PC)`); the address of
 * each cell is the pea's PC (instruction address + 2) plus the displacement.
 * All of the TTY_$K_* entry points read them with `move.w (An),Dnw`
 * (TTY_$K_SET_FUNC_CHAR 0x00E67500, TTY_$K_ENABLE_FUNC 0x00E675E8,
 * TTY_$K_SET_INPUT_FLAG 0x00E67690, TTY_$K_SET_OUTPUT_FLAG 0x00E67728), so
 * every cell is a word, not a byte.  Bytes read out of the image with gsk:
 *   0x00E667C4: 00 02  (term_$const_word_2, shared with TTY_$I_GET_DESC)
 *   0x00E66896: 00 01 00 00            (term_$const_word_1, term_$const_word_0 - shared, see term_internal.h)
 *   0x00E66D82: 00 03 00 0e 00 0d ff 00 00 0a 00 09 00 08
 *               (3, 14, 13, 0xFF byte, 10, 9, 8)
 */
static const uint16_t tty_num_3  = 3;   /* 0x00E66D82: case 2 */
static const uint16_t tty_num_8  = 8;   /* 0x00E66D8E: cases 10, 23 - SUSP */
static const uint16_t tty_num_9  = 9;   /* 0x00E66D8C: cases 25, 26 - DSUSP */
static const uint16_t tty_num_10 = 10;  /* 0x00E66D8A: cases 27, 28 - STATUS */
static const uint16_t tty_num_13 = 13;  /* 0x00E66D86: case 8 - INT */
static const uint16_t tty_num_14 = 14;  /* 0x00E66D84: case 8 - QUIT */

/* The word at 0x00E66D88 is ff 00; case 29 passes its address as the byte
 * value argument (0x00E66A1E pea (0x368,PC)), so only the 0xFF is read. */
static const uint8_t tty_true_byte = 0xff;

/*
 * SIO parameter block: TERM_$CONTROL keeps a 0x16-byte local at (-0x18,A6) and
 * a 32-bit change mask at (-0x1c,A6); both addresses go to SIO_$K_SET_PARAM
 * (0x00E66D3C-0x00E66D48).  The block is sio/sio.h's sio_params_t.  Offsets
 * the assembly touches:
 *   (-0x15,A6) = block + 0x03 -> low byte of flags1
 *   (-0x11,A6) = block + 0x07 -> low byte of flags2
 *   (-0x10,A6) = block + 0x08 -> break_mask
 *   (-0x0c,A6) = block + 0x0C -> high half of baud_rate
 *   (-0x0a,A6) = block + 0x0E -> low half of baud_rate
 *   (-0x08,A6) = block + 0x10 -> char_size
 *   (-0x06,A6) = block + 0x12 -> stop_bits
 *   (-0x04,A6) = block + 0x14 -> parity
 *
 * The original never initialises the whole block: each case writes only the
 * field its change mask selects, and SIO_$K_SET_PARAM only consults the
 * fields named by the mask.  The uninitialised local below is therefore
 * faithful, not an oversight.
 */

// Terminal control options (option codes for TERM_$CONTROL)
#define CTRL_SET_FUNC_CHAR_DEFAULT    0
#define CTRL_SET_FUNC_CHAR_BREAK      1
#define CTRL_SET_FUNC_CHAR_2          2
#define CTRL_FLUSH_SET_RAW            3
#define CTRL_INVERT_INPUT_FLAG        4
#define CTRL_INVERT_OUTPUT_FLAG       5
#define CTRL_SET_SPEED                6
#define CTRL_SET_LINE_FLAG            7
#define CTRL_ENABLE_INT_QUIT          8
#define CTRL_NOP_9                    9
#define CTRL_ENABLE_SUSP             10
#define CTRL_SET_INPUT_FLAG_COND     11
#define CTRL_SET_ECHO                12
#define CTRL_SET_SOMETHING_13        13
#define CTRL_INVALID_14              14
#define CTRL_ENABLE_PGROUP           15
#define CTRL_INVALID_16              16
#define CTRL_SET_FLAG_17             17
#define CTRL_SET_PARITY              18
#define CTRL_SET_DATA_BITS           19
#define CTRL_SET_STOP_BITS           20
#define CTRL_SET_FLOW_CTRL           21
#define CTRL_TIMED_BREAK             22
#define CTRL_SET_FUNC_CHAR_SUSP      23
#define CTRL_NOP_24                  24
#define CTRL_ENABLE_DSUSP            25
#define CTRL_SET_FUNC_CHAR_DSUSP     26
#define CTRL_ENABLE_STATUS           27
#define CTRL_SET_FUNC_CHAR_STATUS    28
#define CTRL_SET_OUTPUT_FLAG_COND    29
#define CTRL_SET_PGROUP              30
#define CTRL_SET_FLAG_31             31
#define CTRL_SET_SPEED_32            32
#define CTRL_FLUSH_INPUT             33
#define CTRL_FLUSH_OUTPUT            34
#define CTRL_DRAIN_OUTPUT            35
#define CTRL_SET_KBD_MODE            36

// Controls terminal settings and behavior.
void TERM_$CONTROL(short *line_ptr, unsigned short *option_ptr, unsigned short *value_ptr,
                   status_$t *status_ret) {
    unsigned short option;
    unsigned char inverted;     /* (-0x36,A6): inverted / narrowed value byte */
    short real_line;
    sio_params_t params;        /* (-0x18,A6) */
    uint32_t param_mask;        /* (-0x1c,A6): 32-bit change mask */
    uid_t *pgroup_ptr;          /* (-0x24,A6) in case 30 */

    option = *option_ptr;                                   /* 0x00E66926 */

    switch (option) {
        /* 0x00E66988..0x00E669D6: six SET_FUNC_CHAR arms sharing one call */
        case CTRL_SET_FUNC_CHAR_DEFAULT:                    /* 0x00E66988 */
            TTY_$K_SET_FUNC_CHAR(line_ptr, &term_$const_word_0, (const char *)value_ptr, status_ret);
            goto convert;

        case CTRL_SET_FUNC_CHAR_BREAK:                      /* 0x00E66994 */
            TTY_$K_SET_FUNC_CHAR(line_ptr, &term_$const_word_2, (const char *)value_ptr, status_ret);
            goto convert;

        case CTRL_SET_FUNC_CHAR_2:                          /* 0x00E669A0 */
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_3, (const char *)value_ptr, status_ret);
            goto convert;

        case CTRL_SET_FUNC_CHAR_SUSP:                       /* 0x00E669AC */
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_8, (const char *)value_ptr, status_ret);
            goto convert;

        case CTRL_SET_FUNC_CHAR_DSUSP:                      /* 0x00E669B8 */
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_9, (const char *)value_ptr, status_ret);
            goto convert;

        case CTRL_SET_FUNC_CHAR_STATUS:                     /* 0x00E669C4 */
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_10, (const char *)value_ptr, status_ret);
            goto convert;

        case CTRL_FLUSH_SET_RAW:                            /* 0x00E669DA */
            /* K_FLUSH_INPUT's status is overwritten by TTY_$I_SET_RAW; the
             * line is passed by value (move.w (A2)) and the value byte in
             * the high half of a word slot (move.b (A0),-(SP)). */
            TTY_$K_FLUSH_INPUT(line_ptr, status_ret);
            TTY_$I_SET_RAW(*line_ptr, (char)*(unsigned char *)value_ptr, status_ret);
            goto convert;                                   /* 0x00E669F6 -> 0x00E66B26 */

        case CTRL_INVERT_OUTPUT_FLAG:                       /* 0x00E669FA */
            /* not.b the caller's byte into (-0x36,A6), pass it by reference
             * with flag number 1. */
            inverted = (unsigned char)~(*(unsigned char *)value_ptr);
            TTY_$K_SET_OUTPUT_FLAG(line_ptr, &term_$const_word_1, (const char *)&inverted, status_ret);
            goto convert;

        case CTRL_SET_OUTPUT_FLAG_COND:                     /* 0x00E66A1C */
            /* The value argument is the constant byte 0xFF at 0x00E66D88,
             * not the caller's value; flag number 1 (joins 0x00E66A0C). */
            TTY_$K_SET_OUTPUT_FLAG(line_ptr, &term_$const_word_1, (const char *)&tty_true_byte,
                                   status_ret);
            goto convert;

        case CTRL_INVERT_INPUT_FLAG:                        /* 0x00E66A24 */
            inverted = (unsigned char)~(*(unsigned char *)value_ptr);
            TTY_$K_SET_INPUT_FLAG(line_ptr, &term_$const_word_0, (const char *)&inverted, status_ret);
            goto convert;

        case CTRL_ENABLE_INT_QUIT:                          /* 0x00E66A48 */
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_13, (const char *)value_ptr, status_ret);
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_14, (const char *)value_ptr, status_ret);
            goto convert;

        case CTRL_SET_INPUT_FLAG_COND:                      /* 0x00E66A74 */
            /* The caller's value pointer goes straight through
             * (`move.l (0x10,A6),-(SP)`); no inversion (joins 0x00E66A3C). */
            TTY_$K_SET_INPUT_FLAG(line_ptr, &term_$const_word_1, (const char *)value_ptr, status_ret);
            goto convert;

        case CTRL_ENABLE_SUSP:                              /* 0x00E66A80 */
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_8, (const char *)value_ptr, status_ret);
            goto set_pgroup_self;                           /* 0x00E66A96 */

        case CTRL_ENABLE_DSUSP:                             /* 0x00E66AA0 */
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_9, (const char *)value_ptr, status_ret);
            goto set_pgroup_self;                           /* 0x00E66AB6 */

        case CTRL_ENABLE_STATUS:                            /* 0x00E66AC0 */
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_10, (const char *)value_ptr, status_ret);
        set_pgroup_self:
            /* 0x00E66AD6..0x00E66AEA: &PROC2_$UNWIRED_DATA.uid[PROC1_$AS_ID]
             * (move.w PROC1_$AS_ID; lsl.w #3; pea (0,A0,D0w)). */
            pgroup_ptr = &PROC2_$UNWIRED_DATA.uid[PROC1_$AS_ID];
            goto set_pgroup;                                /* 0x00E66AF8 */

        case CTRL_SET_PGROUP:                               /* 0x00E66AEC */
            pgroup_ptr = (uid_t *)value_ptr;
        set_pgroup:
            TTY_$K_SET_PGROUP(line_ptr, pgroup_ptr, status_ret);
            goto convert;                                   /* 0x00E66B00 -> 0x00E66D62 */

        case CTRL_FLUSH_INPUT:                              /* 0x00E66B04 */
            TTY_$K_FLUSH_INPUT(line_ptr, status_ret);
            goto convert;

        case CTRL_FLUSH_OUTPUT:                             /* 0x00E66B10 */
            TTY_$K_FLUSH_OUTPUT(line_ptr, status_ret);
            goto convert;

        case CTRL_DRAIN_OUTPUT:                             /* 0x00E66B1C */
            TTY_$K_DRAIN_OUTPUT(line_ptr, status_ret);
            goto convert;                                   /* 0x00E66B26 -> 0x00E66D70 */

        case CTRL_SET_LINE_FLAG:                            /* 0x00E66B2C */
            real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
            if (*status_ret != status_$ok) {                /* 0x00E66B3A tst.l (A3) */
                return;                                     /* 0x00E66D78: no conversion */
            }
            /* 0x00E66B40..0x00E66B56: real_line * 0x38 into DTTE (0x00E2DC90),
             * store the caller's value BYTE at +0x36. */
            TERM_$DATA.dtte[real_line].flags = *(unsigned char *)value_ptr;
            goto convert;                                   /* 0x00E66B5A -> 0x00E66D70 */

        case CTRL_SET_KBD_MODE: {                           /* 0x00E66B5E */
            /* move.w (A0),D0w; move.b D0b,(-0x36,A6): the low byte of the
             * caller's word, passed by reference. */
            inverted = (unsigned char)*value_ptr;
            KBD_$SET_KBD_MODE(line_ptr, &inverted, status_ret);
            goto convert;                                   /* 0x00E66B76 -> 0x00E66D62 */
        }

        case CTRL_SET_FLAG_31:                              /* 0x00E66B7A */
            /* bset.b/bclr.b #0,(-0x11,A6) -> flags2 bit 0 */
            if (*(char *)value_ptr < 0) {
                params.flags2 |= 0x00000001u;
            } else {
                params.flags2 &= ~0x00000001u;
            }
            param_mask = 0x200;
            goto set_sio_param;

        case CTRL_SET_FLAG_17:                              /* 0x00E66B9C */
            /* bset.b/bclr.b #1,(-0x11,A6) -> flags2 bit 1 */
            if (*(char *)value_ptr < 0) {
                params.flags2 |= 0x00000002u;
            } else {
                params.flags2 &= ~0x00000002u;
            }
            param_mask = 0x400;
            goto set_sio_param;

        case CTRL_ENABLE_PGROUP:                            /* 0x00E66BBE */
            /* bset.b/bclr.b #2,(-0x11,A6) -> flags2 bit 2, then SET_PGROUP to
             * this process's UID (its status is overwritten by SET_PARAM). */
            if (*(char *)value_ptr < 0) {
                params.flags2 |= 0x00000004u;
            } else {
                params.flags2 &= ~0x00000004u;
            }
            TTY_$K_SET_PGROUP(line_ptr, &PROC2_$UNWIRED_DATA.uid[PROC1_$AS_ID], status_ret);
            param_mask = 0x800;
            goto set_sio_param;

        case CTRL_SET_SPEED:                                /* 0x00E66C00 */
            /* move.w (A0),(-0xa,A6) / move.w (A0),(-0xc,A6): both halves of
             * baud_rate get the same word. */
            params.baud_rate = ((uint32_t)*value_ptr << 16) | (uint32_t)*value_ptr;
            param_mask = 1;
            goto set_sio_param;

        case CTRL_SET_SPEED_32:                             /* 0x00E66C18 */
            params.baud_rate = ((uint32_t)*value_ptr << 16) | (uint32_t)*value_ptr;
            param_mask = 2;
            goto set_sio_param;

        case CTRL_SET_ECHO:                                 /* 0x00E66C28 */
            /* bset.b/bclr.b #0,(-0x15,A6) -> flags1 bit 0 */
            if (*(char *)value_ptr < 0) {
                params.flags1 |= 0x00000001u;
            } else {
                params.flags1 &= ~0x00000001u;
            }
            param_mask = 0x20;
            goto set_sio_param;

        case CTRL_SET_SOMETHING_13:                         /* 0x00E66C42 */
            /* bset.b/bclr.b #3,(-0x15,A6) -> flags1 bit 3 */
            if (*(char *)value_ptr < 0) {
                params.flags1 |= 0x00000008u;
            } else {
                params.flags1 &= ~0x00000008u;
            }
            param_mask = 0x40;
            goto set_sio_param;

        case CTRL_SET_PARITY:                               /* 0x00E66C5C */
            /* 0x00E66C62..0x00E66C80: 3 -> 3, 1 -> 1, 0 -> 0 into parity;
             * anything else -> 0x00E66CEA (invalid, no conversion). */
            switch (*value_ptr) {
                case 3: params.parity = 3; break;
                case 1: params.parity = 1; break;
                case 0: params.parity = 0; break;
                default:
                    goto invalid_value;
            }
            param_mask = 4;
            goto set_sio_param;

        case CTRL_SET_DATA_BITS:                            /* 0x00E66C90 */
            /*
             * cmpi.w #4,D0w / bcc -> invalid, then the 4-entry jump table at
             * 0x00E66CA6 (0x0008, 0x0010, 0x000C, 0x0014): 0 -> 0x00E66CAE
             * (0), 1 -> 0x00E66CB6 (1), 2 -> 0x00E66CB2 (2), 3 -> 0x00E66CBA
             * (3) into char_size.
             */
            switch (*value_ptr) {
                case 0: params.char_size = 0; break;
                case 1: params.char_size = 1; break;
                case 2: params.char_size = 2; break;
                case 3: params.char_size = 3; break;
                default:
                    goto invalid_value;
            }
            param_mask = 0x10;
            goto set_sio_param;

        case CTRL_SET_STOP_BITS:                            /* 0x00E66CC4 */
            /* 1/2/3 only, into stop_bits */
            switch (*value_ptr) {
                case 1: params.stop_bits = 1; break;
                case 2: params.stop_bits = 2; break;
                case 3: params.stop_bits = 3; break;
                default:
                    goto invalid_value;
            }
            param_mask = 8;
            goto set_sio_param;

        case CTRL_SET_FLOW_CTRL: {                          /* 0x00E66CFC */
            /* btst bits 0..3 of the caller's word into bits 0,1,3,4 of the
             * longword break_mask (0x00E66CFC..0x00E66D30). */
            uint16_t flow = *value_ptr;
            uint32_t bits = 0;
            if (flow & 0x1) bits = 1;
            if (flow & 0x2) bits |= 0x2;
            if (flow & 0x4) bits |= 0x8;
            if (flow & 0x8) bits |= 0x10;
            params.break_mask = bits;
            param_mask = 0x2000;
        }
        set_sio_param:                                      /* 0x00E66D3C */
            SIO_$K_SET_PARAM(line_ptr, &params, &param_mask, status_ret);
            goto convert;                                   /* 0x00E66D4E */

        case CTRL_TIMED_BREAK:                              /* 0x00E66D54 */
            SIO_$K_TIMED_BREAK(line_ptr, value_ptr, status_ret);
            goto convert;                                   /* 0x00E66D62 */

        case CTRL_NOP_9:                                    /* table -> 0x00E66D70 */
        case CTRL_NOP_24:
            goto convert;

        case CTRL_INVALID_14:                               /* table -> 0x00E66D68 */
        case CTRL_INVALID_16:
        default:                                            /* 0x00E66930 bcc, >= 0x25 */
            *status_ret = status_$term_invalid_option;      /* 0x00E66D68 */
            goto convert;                                   /* falls into 0x00E66D70 */
    }

invalid_value:                                              /* 0x00E66CEA */
    *status_ret = status_$term_invalid_option;
    return;                                                 /* bra.w 0x00E66D78: no conversion */

convert:                                                    /* 0x00E66D70 */
    TERM_$STATUS_CONVERT(status_ret);
}
