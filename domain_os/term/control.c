#include "term.h"

// External data
extern short PROC1_$AS_ID;      // at 0xe2060a
extern char PROC2_UID[];        // at 0xe7be94 (UID array)

// External TTY/SIO functions
extern void TTY_$K_FLUSH_INPUT(short *line_ptr, status_$t *status_ret);
extern void TTY_$K_FLUSH_OUTPUT(short *line_ptr, status_$t *status_ret);
extern void TTY_$K_DRAIN_OUTPUT(short *line_ptr, status_$t *status_ret);
extern void TTY_$I_SET_RAW(short line, char raw_mode, status_$t *status_ret);
extern void TTY_$K_SET_FUNC_CHAR(short *line_ptr, void *func_id, unsigned short *char_ptr,
                                  status_$t *status_ret);
extern void TTY_$K_ENABLE_FUNC(short *line_ptr, void *func_id, unsigned short *enable_ptr,
                               status_$t *status_ret);
extern void TTY_$K_SET_INPUT_FLAG(short *line_ptr, void *flag_id, unsigned long value,
                                  status_$t *status_ret);
extern void TTY_$K_SET_OUTPUT_FLAG(short *line_ptr, void *flag_id, unsigned long value,
                                   status_$t *status_ret);
extern void TTY_$K_SET_PGROUP(short *line_ptr, void *pgroup, status_$t *status_ret);
extern void SIO_$K_SET_PARAM(short *line_ptr, void *params, void *mask, status_$t *status_ret);
extern void SIO_$K_TIMED_BREAK(short *line_ptr, unsigned short *duration, status_$t *status_ret);
extern void KBD_$SET_KBD_MODE(short *line_ptr, unsigned char *mode, status_$t *status_ret);

// Function ID constants (these are actually addresses in the original)
static char func_id_default;    // 0xe66898
static char func_id_break;      // 0xe667c4
static char func_id_2;          // 0xe66d82
static char func_id_int;        // 0xe66d86
static char func_id_quit;       // 0xe66d84
static char func_id_susp;       // 0xe66d8e
static char func_id_cond;       // 0xe66896
static char func_id_dsusp;      // 0xe66d8c
static char func_id_status;     // 0xe66d8a

// SIO parameter structure for SIO_$K_SET_PARAM
typedef struct {
    unsigned char unused[3];
    unsigned char flags1;       // offset 3 (-0x15 from end)
    unsigned char flags2;       // offset 7 (-0x11 from end)
    unsigned long param_bits;   // offset 8 (-0x10 from end)
    unsigned short speed_in;    // offset 12 (-0xc from end)
    unsigned short speed_out;   // offset 14 (-0xa from end)
    unsigned short parity;      // offset 16 (-0x8 from end)
    unsigned short stop_bits;   // offset 18 (-0x6 from end)
    unsigned short data_bits;   // offset 20 (-0x4 from end)
} sio_params_t;

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
    unsigned long param_mask;
    void *pgroup_ptr;

    option = *option_ptr;

    switch (option) {
        case CTRL_SET_FUNC_CHAR_DEFAULT:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &func_id_default, value_ptr, status_ret);
            break;

        case CTRL_SET_FUNC_CHAR_BREAK:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &func_id_break, value_ptr, status_ret);
            break;

        case CTRL_SET_FUNC_CHAR_2:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &func_id_2, value_ptr, status_ret);
            break;

        case CTRL_FLUSH_SET_RAW:
            TTY_$K_FLUSH_INPUT(line_ptr, status_ret);
            TTY_$I_SET_RAW(*line_ptr, *(unsigned char *)value_ptr, status_ret);
            goto done_no_convert;

        case CTRL_INVERT_INPUT_FLAG:
            inverted = ~(*(unsigned char *)value_ptr);
            TTY_$K_SET_INPUT_FLAG(line_ptr, &func_id_cond, (unsigned long)inverted, status_ret);
            break;

        case CTRL_INVERT_OUTPUT_FLAG:
            inverted = ~(*(unsigned char *)value_ptr);
            TTY_$K_SET_OUTPUT_FLAG(line_ptr, &func_id_cond, (unsigned long)inverted, status_ret);
            break;

        case CTRL_SET_SPEED:
            params.speed_in = *value_ptr;
            params.speed_out = *value_ptr;
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
            TTY_$K_ENABLE_FUNC(line_ptr, &func_id_int, value_ptr, status_ret);
            TTY_$K_ENABLE_FUNC(line_ptr, &func_id_quit, value_ptr, status_ret);
            break;

        case CTRL_NOP_9:
        case CTRL_NOP_24:
            goto done_no_convert;

        case CTRL_ENABLE_SUSP:
            TTY_$K_ENABLE_FUNC(line_ptr, &func_id_susp, value_ptr, status_ret);
            goto set_pgroup;

        case CTRL_SET_INPUT_FLAG_COND:
            inverted = ~(*(unsigned char *)value_ptr);
            TTY_$K_SET_INPUT_FLAG(line_ptr, &func_id_cond, (unsigned long)inverted, status_ret);
            break;

        case CTRL_SET_ECHO:
            if (*(char *)value_ptr < 0) {
                params.flags2 |= 1;
            } else {
                params.flags2 &= ~1;
            }
            param_mask = 0x20;
            goto set_sio_param;

        case CTRL_SET_SOMETHING_13:
            if (*(char *)value_ptr < 0) {
                params.flags2 |= 8;
            } else {
                params.flags2 &= ~8;
            }
            param_mask = 0x40;
            goto set_sio_param;

        case CTRL_ENABLE_PGROUP:
            if (*(char *)value_ptr < 0) {
                params.flags1 |= 4;
            } else {
                params.flags1 &= ~4;
            }
            pgroup_ptr = (void *)(PROC2_UID + (short)(PROC1_$AS_ID << 3));
            TTY_$K_SET_PGROUP(line_ptr, pgroup_ptr, status_ret);
            param_mask = 0x800;
            goto set_sio_param;

        case CTRL_SET_FLAG_17:
            if (*(char *)value_ptr < 0) {
                params.flags1 |= 2;
            } else {
                params.flags1 &= ~2;
            }
            param_mask = 0x400;
            goto set_sio_param;

        case CTRL_SET_PARITY:
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

        case CTRL_SET_STOP_BITS:
            switch (*value_ptr) {
                case 0: params.stop_bits = 0; break;
                case 1: params.stop_bits = 1; break;
                case 2: params.stop_bits = 2; break;
                case 3: params.stop_bits = 3; break;
                default:
                    *status_ret = status_$term_invalid_option;
                    return;
            }
            param_mask = 0x10;
            goto set_sio_param;

        case CTRL_SET_DATA_BITS:
            switch (*value_ptr) {
                case 1: params.data_bits = 1; break;
                case 2: params.data_bits = 2; break;
                case 3: params.data_bits = 3; break;
                default:
                    *status_ret = status_$term_invalid_option;
                    return;
            }
            param_mask = 8;
            goto set_sio_param;

        case CTRL_SET_FLOW_CTRL: {
            unsigned short flow = *value_ptr;
            unsigned long bits = 0;
            if (flow & 1) bits |= 1;
            if (flow & 2) bits |= 2;
            if (flow & 4) bits |= 8;
            if (flow & 8) bits |= 0x10;
            params.param_bits = bits;
            param_mask = 0x2000;
            goto set_sio_param;
        }

        case CTRL_TIMED_BREAK:
            SIO_$K_TIMED_BREAK(line_ptr, value_ptr, status_ret);
            goto done;

        case CTRL_SET_FUNC_CHAR_SUSP:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &func_id_susp, value_ptr, status_ret);
            break;

        case CTRL_ENABLE_DSUSP:
            TTY_$K_ENABLE_FUNC(line_ptr, &func_id_dsusp, value_ptr, status_ret);
            goto set_pgroup;

        case CTRL_SET_FUNC_CHAR_DSUSP:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &func_id_dsusp, value_ptr, status_ret);
            break;

        case CTRL_ENABLE_STATUS:
            TTY_$K_ENABLE_FUNC(line_ptr, &func_id_status, value_ptr, status_ret);
        set_pgroup:
            pgroup_ptr = (void *)(PROC2_UID + (short)(PROC1_$AS_ID << 3));
            TTY_$K_SET_PGROUP(line_ptr, pgroup_ptr, status_ret);
            goto done;

        case CTRL_SET_FUNC_CHAR_STATUS:
            TTY_$K_SET_FUNC_CHAR(line_ptr, &func_id_status, value_ptr, status_ret);
            break;

        case CTRL_SET_OUTPUT_FLAG_COND:
            inverted = ~(*(unsigned char *)value_ptr);
            TTY_$K_SET_OUTPUT_FLAG(line_ptr, &func_id_cond, (unsigned long)inverted, status_ret);
            break;

        case CTRL_SET_PGROUP:
            pgroup_ptr = (void *)value_ptr;
            TTY_$K_SET_PGROUP(line_ptr, pgroup_ptr, status_ret);
            goto done;

        case CTRL_SET_FLAG_31:
            if (*(char *)value_ptr < 0) {
                params.flags1 |= 1;
            } else {
                params.flags1 &= ~1;
            }
            param_mask = 0x200;
            goto set_sio_param;

        case CTRL_SET_SPEED_32:
            params.speed_in = *value_ptr;
            params.speed_out = *value_ptr;
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
