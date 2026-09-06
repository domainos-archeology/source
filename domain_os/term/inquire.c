#include "term/term_internal.h"

// Function ID constants (addresses in original)
static char func_id_default;    // 0xe66898
static char func_id_break;      // 0xe667c4
static char func_id_2;          // 0xe66d82
static char func_id_susp;       // 0xe66d8e
static char func_id_dsusp;      // 0xe66d8c
static char func_id_status;     // 0xe66d8a

/*
 * SIO parameter block: TERM_$INQUIRE keeps a 0x16-byte local at (-0x18,A6) and
 * hands its address to SIO_$K_INQ_PARAM, which fills it from the SIO
 * descriptor's +0x4C block (SIO_$K_INQ_PARAM 0x00E68356-0x00E68362 copies five
 * longwords and one word).  That is exactly sio/sio.h's sio_params_t, so this
 * file now uses that type directly.  Offsets referenced by the assembly:
 *   (-0x15,A6) = block + 0x03 -> low byte of flags1
 *   (-0x11,A6) = block + 0x07 -> low byte of flags2
 *   (-0x0d,A6) = block + 0x0B -> low byte of break_mask
 *   (-0x0a,A6) = block + 0x0E -> low half of baud_rate
 *   (-0x08,A6) = block + 0x10 -> char_size
 *   (-0x06,A6) = block + 0x12 -> stop_bits
 *   (-0x04,A6) = block + 0x14 -> parity
 *
 * Selector longwords in the code region that the original passes to
 * SIO_$K_INQ_PARAM by address (`pea (d,PC)`); read out of the image with gsk.
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

// Inquire option codes
#define INQ_FUNC_CHAR_DEFAULT    0
#define INQ_FUNC_CHAR_BREAK      1
#define INQ_FUNC_CHAR_2          2
#define INQ_RAW_MODE             3
#define INQ_INPUT_FLAG           4
#define INQ_OUTPUT_FLAG          5
#define INQ_SPEED                6
#define INQ_LINE_FLAG            7
#define INQ_INT_QUIT_ENABLED     8
#define INQ_NOP_9                9
#define INQ_SUSP_ENABLED        10
#define INQ_INPUT_FLAG_COND     11
#define INQ_ECHO                12
#define INQ_SOMETHING_13        13
#define INQ_SOMETHING_14        14
#define INQ_PGROUP_ENABLED      15
#define INQ_SOMETHING_16        16
#define INQ_FLAG_17             17
#define INQ_PARITY              18
#define INQ_DATA_BITS           19   /* char size, block + 0x10 */
#define INQ_STOP_BITS           20   /* stop bits, block + 0x12 */
#define INQ_FLOW_CTRL           21
#define INQ_FUNC_CHAR_SUSP      23
#define INQ_NOP_24              24
#define INQ_DSUSP_ENABLED       25
#define INQ_FUNC_CHAR_DSUSP     26
#define INQ_STATUS_ENABLED      27
#define INQ_FUNC_CHAR_STATUS    28
#define INQ_OUTPUT_FLAG_COND    29
#define INQ_PGROUP              30
#define INQ_FLAG_31             31
#define INQ_SPEED_32            32

// Inquires terminal settings.
void TERM_$INQUIRE(short *line_ptr, unsigned short *option_ptr, unsigned short *value_ret,
                   status_$t *status_ret) {
    unsigned short option;
    short real_line;
    uint32_t flags;
    uint32_t func_enabled;
    sio_params_t params;
    uid_t pgroup;
    char raw_mode_temp;

    option = *option_ptr;

    switch (option) {
        case INQ_FUNC_CHAR_DEFAULT:
            TTY_$K_INQ_FUNC_CHAR(line_ptr, &func_id_default, value_ret, status_ret);
            break;

        case INQ_FUNC_CHAR_BREAK:
            TTY_$K_INQ_FUNC_CHAR(line_ptr, &func_id_break, value_ret, status_ret);
            break;

        case INQ_FUNC_CHAR_2:
            TTY_$K_INQ_FUNC_CHAR(line_ptr, &func_id_2, value_ret, status_ret);
            break;

        case INQ_RAW_MODE:
            TTY_$I_INQ_RAW(*line_ptr, &raw_mode_temp, status_ret);
            *(unsigned char *)value_ret = raw_mode_temp;
            break;

        case INQ_INPUT_FLAG:
            TTY_$K_INQ_INPUT_FLAGS(line_ptr, &flags, status_ret);
            *(unsigned char *)value_ret = (flags & 1) ? 0 : 0xFF;
            break;

        case INQ_OUTPUT_FLAG:
        case INQ_OUTPUT_FLAG_COND:
            TTY_$K_INQ_OUTPUT_FLAGS(line_ptr, &flags, status_ret);
            *(unsigned char *)value_ret = (flags & 2) ? 0xFF : 0;
            break;

        case INQ_SPEED:
        case INQ_SPEED_32:
            /* 00e66fac..00e66fca: move.w (-0xa,A6),(A0) = low half of baud_rate */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_speed, status_ret);
            *value_ret = (uint16_t)params.baud_rate;
            break;

        case INQ_LINE_FLAG:
            real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }
            *(unsigned char *)value_ret = TERM_$DATA.dtte[real_line].flags;
            break;

        case INQ_INT_QUIT_ENABLED:
            TTY_$K_INQ_FUNC_ENABLED(line_ptr, &func_enabled, status_ret);
            *(unsigned char *)value_ret = ((func_enabled & 0x4000) && (func_enabled & 0x2000))
                                          ? 0xFF : 0;
            break;

        case INQ_NOP_9:
            *value_ret = 0;
            break;

        case INQ_SUSP_ENABLED:
            TTY_$K_INQ_FUNC_ENABLED(line_ptr, &func_enabled, status_ret);
            *(unsigned char *)value_ret = (func_enabled & 0x100) ? 0xFF : 0;
            break;

        case INQ_INPUT_FLAG_COND:
            TTY_$K_INQ_INPUT_FLAGS(line_ptr, &flags, status_ret);
            *(unsigned char *)value_ret = (flags & 2) ? 0xFF : 0;
            break;

        case INQ_ECHO:
            /* 00e67012: btst.b #0,(-0x15,A6) -> flags1 bit 0 */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags1_b0, status_ret);
            *(char *)value_ret = (params.flags1 & 0x00000001u) ? 0xFF : 0;
            break;

        case INQ_SOMETHING_13:
            /* 00e67032: btst.b #3,(-0x15,A6) -> flags1 bit 3 */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags1_b3, status_ret);
            *(char *)value_ret = (params.flags1 & 0x00000008u) ? 0xFF : 0;
            break;

        case INQ_SOMETHING_14:
            /* 00e67070: btst.b #2,(-0x15,A6) -> flags1 bit 2 */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags1_b2, status_ret);
            *(char *)value_ret = (params.flags1 & 0x00000004u) ? 0xFF : 0;
            break;

        case INQ_PGROUP_ENABLED:
            /* 00e6708e: btst.b #2,(-0x11,A6) -> flags2 bit 2 */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags2_b2, status_ret);
            *(char *)value_ret = (params.flags2 & 0x00000004u) ? 0xFF : 0;
            break;

        case INQ_SOMETHING_16:
            /* 00e67052: btst.b #1,(-0x15,A6) -> flags1 bit 1 */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags1_b1, status_ret);
            *(char *)value_ret = (params.flags1 & 0x00000002u) ? 0xFF : 0;
            break;

        case INQ_FLAG_17:
            /* 00e670ac: btst.b #1,(-0x11,A6) -> flags2 bit 1 */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags2_b1, status_ret);
            *(char *)value_ret = (params.flags2 & 0x00000002u) ? 0xFF : 0;
            break;

        case INQ_PARITY:
            /* 00e670f2..00e6713a: move.w (-0x4,A6),D1w = parity */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_parity, status_ret);
            if (params.parity == 3) {
                *value_ret = 3;
            } else if (params.parity == 1) {
                *value_ret = 1;
            } else if (params.parity == 0) {
                *value_ret = 0;
            }
            break;

        case INQ_DATA_BITS:
            /* 00e66fce..00e66fec: move.w (-0x8,A6),(A0) = char_size */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_char_size, status_ret);
            *value_ret = (uint16_t)params.char_size;
            break;

        case INQ_STOP_BITS:
            /* 00e66ff0..00e6700e: move.w (-0x6,A6),(A0) = stop_bits */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_stop_bits, status_ret);
            *value_ret = (uint16_t)params.stop_bits;
            break;

        case INQ_FLOW_CTRL: {
            unsigned short flow = 0;
            /* 00e6713c..00e67192: btst.b #0/#1/#3/#4,(-0xd,A6) -> the low
             * byte of break_mask, gathered into bits 0..3 of a local word. */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flow_ctrl, status_ret);
            if (params.break_mask & 0x00000001u) flow |= 1;
            if (params.break_mask & 0x00000002u) flow |= 2;
            if (params.break_mask & 0x00000008u) flow |= 4;
            if (params.break_mask & 0x00000010u) flow |= 8;
            *value_ret = flow;
            break;
        }

        case INQ_FUNC_CHAR_SUSP:
            TTY_$K_INQ_FUNC_CHAR(line_ptr, &func_id_susp, value_ret, status_ret);
            break;

        case INQ_NOP_24:
            *(unsigned char *)value_ret = 0;
            break;

        case INQ_DSUSP_ENABLED:
            TTY_$K_INQ_FUNC_ENABLED(line_ptr, &func_enabled, status_ret);
            *(unsigned char *)value_ret = (func_enabled & 0x200) ? 0xFF : 0;
            break;

        case INQ_FUNC_CHAR_DSUSP:
            TTY_$K_INQ_FUNC_CHAR(line_ptr, &func_id_dsusp, value_ret, status_ret);
            break;

        case INQ_STATUS_ENABLED:
            TTY_$K_INQ_FUNC_ENABLED(line_ptr, &func_enabled, status_ret);
            *(unsigned char *)value_ret = (func_enabled & 0x400) ? 0xFF : 0;
            break;

        case INQ_FUNC_CHAR_STATUS:
            TTY_$K_INQ_FUNC_CHAR(line_ptr, &func_id_status, value_ret, status_ret);
            break;

        case INQ_PGROUP:
            TTY_$K_INQ_PGROUP(line_ptr, &pgroup, status_ret);
            ((uid_t *)value_ret)->high = pgroup.high;
            ((uid_t *)value_ret)->low = pgroup.low;
            break;

        case INQ_FLAG_31:
            /* 00e670ca: btst.b #0,(-0x11,A6) -> flags2 bit 0 */
            SIO_$K_INQ_PARAM(line_ptr, &params, &sio_sel_flags2_b0, status_ret);
            *(char *)value_ret = (params.flags2 & 0x00000001u) ? 0xFF : 0;
            break;

        default:
            *status_ret = status_$term_invalid_option;
            return;
    }

    TERM_$STATUS_CONVERT(status_ret);
}
