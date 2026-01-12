#include "term.h"

// DTTE offsets
#define DTTE_SIZE       0x38
#define DTTE_FLAG_OFFSET  0x12D6  // flag byte at offset +0x36 from base

// External data
extern char TERM_$DTTE_BASE[];  // at 0xe2c9f0

// External TTY/SIO functions
extern void TTY_$I_INQ_RAW(short line, unsigned short *raw_ret, status_$t *status_ret);
extern void TTY_$K_INQ_FUNC_CHAR(short *line_ptr, void *func_id, unsigned short *char_ret,
                                  status_$t *status_ret);
extern void TTY_$K_INQ_FUNC_ENABLED(short *line_ptr, unsigned long *enabled_ret,
                                     status_$t *status_ret);
extern void TTY_$K_INQ_INPUT_FLAGS(short *line_ptr, unsigned long *flags_ret,
                                    status_$t *status_ret);
extern void TTY_$K_INQ_OUTPUT_FLAGS(short *line_ptr, unsigned long *flags_ret,
                                     status_$t *status_ret);
extern void TTY_$K_INQ_PGROUP(short *line_ptr, uid_$t *pgroup_ret, status_$t *status_ret);
extern void SIO_$K_INQ_PARAM(short *line_ptr, void *params, void *mask, status_$t *status_ret);

// Function ID constants (addresses in original)
static char func_id_default;    // 0xe66898
static char func_id_break;      // 0xe667c4
static char func_id_2;          // 0xe66d82
static char func_id_susp;       // 0xe66d8e
static char func_id_dsusp;      // 0xe66d8c
static char func_id_status;     // 0xe66d8a

// SIO parameter structure
typedef struct {
    unsigned char unused[3];
    unsigned char flags1;       // offset 3
    unsigned char padding[3];
    unsigned char flags2;       // offset 7
    unsigned long param_bits;   // offset 8
    unsigned short speed_in;    // offset 12
    unsigned short speed_out;   // offset 14
    unsigned short parity;      // offset 16
    unsigned short stop_bits;   // offset 18
    unsigned short data_bits;   // offset 20
} sio_params_t;

// Inquire option codes (parallel to CTRL_ codes)
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
#define INQ_STOP_BITS           19
#define INQ_DATA_BITS           20
#define INQ_FLOW_CTRL           21
// 22 is not used for inquire
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
//
// This is the query counterpart to TERM_$CONTROL, returning current values
// for various terminal settings.
void TERM_$INQUIRE(short *line_ptr, unsigned short *option_ptr, unsigned short *value_ret,
                   status_$t *status_ret) {
    unsigned short option;
    short real_line;
    int dtte_offset;
    unsigned long flags;
    unsigned long func_enabled;
    sio_params_t params;
    uid_$t pgroup;

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
            TTY_$I_INQ_RAW(*line_ptr, value_ret, status_ret);
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
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *value_ret = params.speed_in;
            break;

        case INQ_LINE_FLAG:
            real_line = TERM_$GET_REAL_LINE(*line_ptr, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }
            dtte_offset = (short)(real_line * DTTE_SIZE);
            *(unsigned char *)value_ret = TERM_$DTTE_BASE[0x12D6 + dtte_offset];
            break;

        case INQ_INT_QUIT_ENABLED:
            TTY_$K_INQ_FUNC_ENABLED(line_ptr, &func_enabled, status_ret);
            // Both interrupt (0x2000) and quit (0x4000) must be enabled
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
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *(char *)value_ret = (params.flags2 & 1) ? 0xFF : 0;
            break;

        case INQ_SOMETHING_13:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *(char *)value_ret = (params.flags2 & 8) ? 0xFF : 0;
            break;

        case INQ_SOMETHING_14:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *(char *)value_ret = (params.flags2 & 4) ? 0xFF : 0;
            break;

        case INQ_PGROUP_ENABLED:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *(char *)value_ret = (params.flags1 & 4) ? 0xFF : 0;
            break;

        case INQ_SOMETHING_16:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *(char *)value_ret = (params.flags2 & 2) ? 0xFF : 0;
            break;

        case INQ_FLAG_17:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *(char *)value_ret = (params.flags1 & 2) ? 0xFF : 0;
            break;

        case INQ_PARITY:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            if (params.parity == 3) {
                *value_ret = 3;
            } else if (params.parity == 1) {
                *value_ret = 1;
            } else if (params.parity == 0) {
                *value_ret = 0;
            }
            break;

        case INQ_STOP_BITS:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *value_ret = params.stop_bits;
            break;

        case INQ_DATA_BITS:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *value_ret = params.data_bits;
            break;

        case INQ_FLOW_CTRL: {
            unsigned short flow = 0;
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            if (params.param_bits & 1) flow |= 1;
            if (params.param_bits & 2) flow |= 2;
            if (params.param_bits & 8) flow |= 4;
            if (params.param_bits & 0x10) flow |= 8;
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
            // Copy the full 8-byte UID to the return buffer
            ((uid_$t *)value_ret)->high = pgroup.high;
            ((uid_$t *)value_ret)->low = pgroup.low;
            break;

        case INQ_FLAG_31:
            SIO_$K_INQ_PARAM(line_ptr, &params, NULL, status_ret);
            *(char *)value_ret = (params.flags1 & 1) ? 0xFF : 0;
            break;

        default:
            *status_ret = status_$term_invalid_option;
            return;
    }

    TERM_$STATUS_CONVERT(status_ret);
}
