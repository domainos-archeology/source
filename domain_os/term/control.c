#include "term/term_internal.h"

/*
 * TTY function / flag numbers.  The Pascal compiler placed these literals in
 * the code region and passes them by reference (`pea (d,PC)`); the address of
 * each cell is the pea's PC (instruction address + 2) plus the displacement.
 * All of the TTY_$K_* entry points read them with `move.w (An),Dnw`
 * (TTY_$K_SET_FUNC_CHAR 0x00E67500, TTY_$K_ENABLE_FUNC 0x00E675E8,
 * TTY_$K_SET_INPUT_FLAG 0x00E67690, TTY_$K_SET_OUTPUT_FLAG 0x00E67728), so
 * every cell is a word, not a byte.  Values read out of the image with gsk.
 */
static const uint16_t tty_num_0  = 0;   /* 0x00E66898 */
static const uint16_t tty_num_1  = 1;   /* 0x00E66896 */
static const uint16_t tty_num_2  = 2;   /* 0x00E667C4 */
static const uint16_t tty_num_3  = 3;   /* 0x00E66D82 */
static const uint16_t tty_num_8  = 8;   /* 0x00E66D8E - SUSP */
static const uint16_t tty_num_9  = 9;   /* 0x00E66D8C - DSUSP */
static const uint16_t tty_num_10 = 10;  /* 0x00E66D8A - STATUS */
static const uint16_t tty_num_13 = 13;  /* 0x00E66D86 - INT */
static const uint16_t tty_num_14 = 14;  /* 0x00E66D84 - QUIT */

/* Byte constant 0xFF (a true Domain boolean) at 0x00E66D88, passed by
 * reference as the value argument of case 29 (0x00E66A1E pea (0x368,PC)). */
static const uint8_t tty_true_byte = 0xff;

/*
 * SIO parameter block: TERM_$CONTROL keeps a 0x16-byte local at (-0x18,A6) and
 * a 32-bit change mask at (-0x1c,A6); both addresses go to SIO_$K_SET_PARAM
 * (0x00E66D3C-0x00E66D48).  The block is sio/sio.h's sio_params_t, so this
 * file now uses that type directly.  Offsets the assembly touches:
 *   (-0x15,A6) = block + 0x03 -> low byte of flags1
 *   (-0x11,A6) = block + 0x07 -> low byte of flags2
 *   (-0x10,A6) = block + 0x08 -> break_mask
 *   (-0x0c,A6) = block + 0x0C -> high half of baud_rate
 *   (-0x0a,A6) = block + 0x0E -> low half of baud_rate
 *   (-0x08,A6) = block + 0x10 -> char_size
 *   (-0x06,A6) = block + 0x12 -> stop_bits
 *   (-0x04,A6) = block + 0x14 -> parity
 *
 * Note the original never initialises the whole block: each case writes only
 * the field its change mask selects, and SIO_$K_SET_PARAM (0x00E680F0 onward)
 * only consults the fields named by the mask.  The uninitialised local below
 * is therefore faithful, not an oversight.
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
#define CTRL_ENABLE_PGROUP           15
#define CTRL_SET_FLAG_17             17
#define CTRL_SET_PARITY              18
#define CTRL_SET_STOP_BITS           19
#define CTRL_SET_DATA_BITS           20
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
    unsigned char inverted;
    short real_line;
    sio_params_t params;
    uint32_t param_mask;        /* 32-bit change mask (SIO_$K_SET_PARAM) */
    void *pgroup_ptr;

    option = *option_ptr;

    switch (option) {
        case CTRL_SET_FUNC_CHAR_DEFAULT:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_0, (char *)value_ptr, status_ret);
            break;

        case CTRL_SET_FUNC_CHAR_BREAK:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_2, (char *)value_ptr, status_ret);
            break;

        case CTRL_SET_FUNC_CHAR_2:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_3, (char *)value_ptr, status_ret);
            break;

        case CTRL_FLUSH_SET_RAW:
            TTY_$K_FLUSH_INPUT(line_ptr, status_ret);
            TTY_$I_SET_RAW(*line_ptr, *(unsigned char *)value_ptr, status_ret);
            goto done_no_convert;

        case CTRL_INVERT_INPUT_FLAG:
            /* 00e66a24: not.b the caller's byte into the local at (-0x36,A6),
             * then pass that local by reference with flag number 0. */
            inverted = ~(*(unsigned char *)value_ptr);
            TTY_$K_SET_INPUT_FLAG(line_ptr, &tty_num_0, (char *)&inverted, status_ret);
            break;

        case CTRL_INVERT_OUTPUT_FLAG:
            /* 00e669fa: same inversion, flag number 1. */
            inverted = ~(*(unsigned char *)value_ptr);
            TTY_$K_SET_OUTPUT_FLAG(line_ptr, &tty_num_1, (char *)&inverted, status_ret);
            break;

        case CTRL_SET_SPEED:
            /* 00e66c00: move.w (A0),(-0xa,A6) / move.w (A0),(-0xc,A6):
             * both halves of baud_rate get the same value. */
            params.baud_rate = ((uint32_t)*value_ptr << 16) | (uint32_t)*value_ptr;
            param_mask = 1;
            goto set_sio_param;

        case CTRL_SET_LINE_FLAG:
            real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }
            TERM_$DATA.dtte[real_line].flags = *(unsigned char *)value_ptr;
            goto done_no_convert;

        case CTRL_ENABLE_INT_QUIT:
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_13, (char *)value_ptr, status_ret);
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_14, (char *)value_ptr, status_ret);
            break;

        case CTRL_NOP_9:
        case CTRL_NOP_24:
            goto done_no_convert;

        case CTRL_ENABLE_SUSP:
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_8, (char *)value_ptr, status_ret);
            goto set_pgroup;

        case CTRL_SET_INPUT_FLAG_COND:
            /* 00e66a74: the caller's value pointer goes straight through
             * (`move.l (0x10,A6),-(SP)`); there is no inversion here. */
            TTY_$K_SET_INPUT_FLAG(line_ptr, &tty_num_1, (char *)value_ptr, status_ret);
            break;

        case CTRL_SET_ECHO:
            /* 00e66c28: bset.b/bclr.b #0,(-0x15,A6) -> flags1 bit 0 */
            if (*(char *)value_ptr < 0) {
                params.flags1 |= 0x00000001u;
            } else {
                params.flags1 &= ~0x00000001u;
            }
            param_mask = 0x20;
            goto set_sio_param;

        case CTRL_SET_SOMETHING_13:
            /* 00e66c42: bset.b/bclr.b #3,(-0x15,A6) -> flags1 bit 3 */
            if (*(char *)value_ptr < 0) {
                params.flags1 |= 0x00000008u;
            } else {
                params.flags1 &= ~0x00000008u;
            }
            param_mask = 0x40;
            goto set_sio_param;

        case CTRL_ENABLE_PGROUP:
            /* 00e66bbe: bset.b/bclr.b #2,(-0x11,A6) -> flags2 bit 2 */
            if (*(char *)value_ptr < 0) {
                params.flags2 |= 0x00000004u;
            } else {
                params.flags2 &= ~0x00000004u;
            }
            pgroup_ptr = (void *)((char *)&PROC2_UID + (short)(PROC1_$AS_ID << 3));
            TTY_$K_SET_PGROUP(line_ptr, pgroup_ptr, status_ret);
            param_mask = 0x800;
            goto set_sio_param;

        case CTRL_SET_FLAG_17:
            /* 00e66b9c: bset.b/bclr.b #1,(-0x11,A6) -> flags2 bit 1 */
            if (*(char *)value_ptr < 0) {
                params.flags2 |= 0x00000002u;
            } else {
                params.flags2 &= ~0x00000002u;
            }
            param_mask = 0x400;
            goto set_sio_param;

        case CTRL_SET_PARITY:
            /* 00e66c5c..00e66c8c: move.w D0w,(-0x4,A6) -> parity */
            switch (*value_ptr) {
                case 0: params.parity = 0; break;
                case 1: params.parity = 1; break;
                case 3: params.parity = 3; break;
                default:
                    *status_ret = status_$term_invalid_option;
                    return;
            }
            param_mask = 4;
            goto set_sio_param;

        case CTRL_SET_DATA_BITS:
            /*
             * 00e66c90: cmpi.w #4,D0w / bcc -> invalid, then a 4-entry jump
             * table at 0x00E66CA6 (0x0008, 0x0010, 0x000C, 0x0014) that maps
             * 0->0, 1->1, 2->2, 3->3 into (-0x8,A6) = char_size.
             */
            switch (*value_ptr) {
                case 0: params.char_size = 0; break;
                case 1: params.char_size = 1; break;
                case 2: params.char_size = 2; break;
                case 3: params.char_size = 3; break;
                default:
                    *status_ret = status_$term_invalid_option;
                    return;
            }
            param_mask = 0x10;
            goto set_sio_param;

        case CTRL_SET_STOP_BITS:
            /* 00e66cc4..00e66cfa: 1/2/3 only, into (-0x6,A6) = stop_bits */
            switch (*value_ptr) {
                case 1: params.stop_bits = 1; break;
                case 2: params.stop_bits = 2; break;
                case 3: params.stop_bits = 3; break;
                default:
                    *status_ret = status_$term_invalid_option;
                    return;
            }
            param_mask = 8;
            goto set_sio_param;

        case CTRL_SET_FLOW_CTRL: {
            unsigned short flow = *value_ptr;
            unsigned long bits = 0;
            /* 00e66cfc..00e66d30: btst bits 0..3 of the caller's word into
             * bits 0,1,3,4 of (-0x10,A6) = break_mask. */
            if (flow & 1) bits |= 1;
            if (flow & 2) bits |= 2;
            if (flow & 4) bits |= 8;
            if (flow & 8) bits |= 0x10;
            params.break_mask = bits;
            param_mask = 0x2000;
            goto set_sio_param;
        }

        case CTRL_TIMED_BREAK:
            SIO_$K_TIMED_BREAK(line_ptr, value_ptr, status_ret);
            goto done;

        case CTRL_SET_FUNC_CHAR_SUSP:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_8, (char *)value_ptr, status_ret);
            break;

        case CTRL_ENABLE_DSUSP:
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_9, (char *)value_ptr, status_ret);
            goto set_pgroup;

        case CTRL_SET_FUNC_CHAR_DSUSP:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_9, (char *)value_ptr, status_ret);
            break;

        case CTRL_ENABLE_STATUS:
            TTY_$K_ENABLE_FUNC(line_ptr, &tty_num_10, (char *)value_ptr, status_ret);
        set_pgroup:
            pgroup_ptr = (void *)((char *)&PROC2_UID + (short)(PROC1_$AS_ID << 3));
            TTY_$K_SET_PGROUP(line_ptr, pgroup_ptr, status_ret);
            goto done;

        case CTRL_SET_FUNC_CHAR_STATUS:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &tty_num_10, (char *)value_ptr, status_ret);
            break;

        case CTRL_SET_OUTPUT_FLAG_COND:
            /* 00e66a1c: the value argument is the constant byte 0xFF at
             * 0x00E66D88, not the caller's value; flag number 1. */
            TTY_$K_SET_OUTPUT_FLAG(line_ptr, &tty_num_1, (char *)&tty_true_byte,
                                   status_ret);
            break;

        case CTRL_SET_PGROUP:
            pgroup_ptr = (void *)value_ptr;
            TTY_$K_SET_PGROUP(line_ptr, pgroup_ptr, status_ret);
            goto done;

        case CTRL_SET_FLAG_31:
            /* 00e66b7a: bset.b/bclr.b #0,(-0x11,A6) -> flags2 bit 0 */
            if (*(char *)value_ptr < 0) {
                params.flags2 |= 0x00000001u;
            } else {
                params.flags2 &= ~0x00000001u;
            }
            param_mask = 0x200;
            goto set_sio_param;

        case CTRL_SET_SPEED_32:
            /* 00e66c18: same two halves as CTRL_SET_SPEED, mask 2 */
            params.baud_rate = ((uint32_t)*value_ptr << 16) | (uint32_t)*value_ptr;
            param_mask = 2;
        set_sio_param:
            SIO_$K_SET_PARAM(line_ptr, &params, &param_mask, status_ret);
            break;

        case CTRL_FLUSH_INPUT:
            TTY_$K_FLUSH_INPUT(line_ptr, status_ret);
            goto done_no_convert;

        case CTRL_FLUSH_OUTPUT:
            TTY_$K_FLUSH_OUTPUT(line_ptr, status_ret);
            goto done_no_convert;

        case CTRL_DRAIN_OUTPUT:
            TTY_$K_DRAIN_OUTPUT(line_ptr, status_ret);
            goto done_no_convert;

        case CTRL_SET_KBD_MODE: {
            unsigned char mode = (unsigned char)*value_ptr;
            KBD_$SET_KBD_MODE(line_ptr, &mode, status_ret);
            break;
        }

        default:
            *status_ret = status_$term_invalid_option;
            return;
    }

done:
    TERM_$STATUS_CONVERT(status_ret);
    return;

done_no_convert:
    return;
}
