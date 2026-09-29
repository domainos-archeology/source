/*
 * term/test/test_control.c - Unit tests for TERM_$CONTROL (0x00E66916)
 *
 * Every callee is a recording stub, so the tests pin the dispatch table:
 * which TTY_$K_* / SIO_$K_* entry each option reaches, the by-reference
 * function-number constants, the value transformations (not.b for cases 4/5,
 * the constant 0xFF byte for case 29, the low byte for case 36, the doubled
 * baud word), the sio_params_t field and change mask each SIO case writes,
 * and -- the bug the re-emission fixed -- exactly which exits go through
 * TERM_$STATUS_CONVERT (0x00E66D70) and which return raw (0x00E66D78).
 */

#include <stdio.h>
#include <string.h>

#include "term/term_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Globals the function reads
 * ============================================================================ */

term_data_t TERM_$DATA;
const uint16_t term_$const_word_2 = 2;     /* the 0x00E667C4 cell, from term_data.c */
const uint16_t term_$const_word_1 = 1;     /* 0x00E66896, from term/read.c */
const uint16_t term_$const_word_0 = 0;     /* 0x00E66898, from term/read.c */
MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);
uint16_t PROC1_$AS_ID;

/* ============================================================================
 * Recording stubs
 * ============================================================================ */

enum {
    CALL_NONE,
    CALL_SET_FUNC_CHAR,
    CALL_ENABLE_FUNC,
    CALL_SET_INPUT_FLAG,
    CALL_SET_OUTPUT_FLAG,
    CALL_FLUSH_INPUT,
    CALL_FLUSH_OUTPUT,
    CALL_DRAIN_OUTPUT,
    CALL_I_SET_RAW,
    CALL_SET_PGROUP,
    CALL_GET_REAL_LINE,
    CALL_SET_PARAM,
    CALL_TIMED_BREAK,
    CALL_SET_KBD_MODE,
    CALL_STATUS_CONVERT
};

#define MAX_CALLS 8
static int call_log[MAX_CALLS];
static int call_count;

static uint16_t func_num_log[MAX_CALLS];
static uint8_t  value_byte_log[MAX_CALLS];
static const void *value_ptr_log[MAX_CALLS];
static uid_t *pgroup_log;
static sio_params_t param_log;
static uint32_t mask_log;
static status_$t callee_status;         /* what every stub stores */
static status_$t get_real_line_status;  /* what TERM_$GET_REAL_LINE stores */
static short get_real_line_result;
static short i_set_raw_line;
static char  i_set_raw_value;
static short line_seen;

static void log_call(int which)
{
    if (call_count < MAX_CALLS) {
        call_log[call_count] = which;
    }
    call_count++;
}

void TTY_$K_SET_FUNC_CHAR(short *line_ptr, const uint16_t *func_ptr,
                          const char *ch_ptr, status_$t *status)
{
    line_seen = *line_ptr;
    func_num_log[call_count] = *func_ptr;
    value_byte_log[call_count] = (uint8_t)*ch_ptr;
    value_ptr_log[call_count] = ch_ptr;
    log_call(CALL_SET_FUNC_CHAR);
    *status = callee_status;
}

void TTY_$K_ENABLE_FUNC(short *line_ptr, const uint16_t *func_ptr,
                        const char *enable_ptr, status_$t *status)
{
    line_seen = *line_ptr;
    func_num_log[call_count] = *func_ptr;
    value_byte_log[call_count] = (uint8_t)*enable_ptr;
    value_ptr_log[call_count] = enable_ptr;
    log_call(CALL_ENABLE_FUNC);
    *status = callee_status;
}

void TTY_$K_SET_INPUT_FLAG(short *line_ptr, const uint16_t *flag_ptr,
                           const char *value_ptr, status_$t *status)
{
    line_seen = *line_ptr;
    func_num_log[call_count] = *flag_ptr;
    value_byte_log[call_count] = (uint8_t)*value_ptr;
    value_ptr_log[call_count] = value_ptr;
    log_call(CALL_SET_INPUT_FLAG);
    *status = callee_status;
}

void TTY_$K_SET_OUTPUT_FLAG(short *line_ptr, const uint16_t *flag_ptr,
                            const char *value_ptr, status_$t *status)
{
    line_seen = *line_ptr;
    func_num_log[call_count] = *flag_ptr;
    value_byte_log[call_count] = (uint8_t)*value_ptr;
    value_ptr_log[call_count] = value_ptr;
    log_call(CALL_SET_OUTPUT_FLAG);
    *status = callee_status;
}

void TTY_$K_FLUSH_INPUT(short *line_ptr, status_$t *status)
{
    line_seen = *line_ptr;
    log_call(CALL_FLUSH_INPUT);
    *status = callee_status;
}

void TTY_$K_FLUSH_OUTPUT(short *line_ptr, status_$t *status)
{
    line_seen = *line_ptr;
    log_call(CALL_FLUSH_OUTPUT);
    *status = callee_status;
}

void TTY_$K_DRAIN_OUTPUT(short *line_ptr, status_$t *status)
{
    line_seen = *line_ptr;
    log_call(CALL_DRAIN_OUTPUT);
    *status = callee_status;
}

void TTY_$I_SET_RAW(short line, char raw, status_$t *status)
{
    i_set_raw_line = line;
    i_set_raw_value = raw;
    log_call(CALL_I_SET_RAW);
    *status = callee_status;
}

void TTY_$K_SET_PGROUP(short *line_ptr, uid_t *uid_ptr, status_$t *status)
{
    line_seen = *line_ptr;
    pgroup_log = uid_ptr;
    log_call(CALL_SET_PGROUP);
    *status = callee_status;
}

short TERM_$GET_REAL_LINE(short line_num, status_$t *status_ret)
{
    line_seen = line_num;
    log_call(CALL_GET_REAL_LINE);
    *status_ret = get_real_line_status;
    return get_real_line_result;
}

void SIO_$K_SET_PARAM(int16_t *line_ptr, sio_params_t *params,
                      const uint32_t *change_mask_ptr, status_$t *status_ret)
{
    line_seen = *line_ptr;
    param_log = *params;
    mask_log = *change_mask_ptr;
    log_call(CALL_SET_PARAM);
    *status_ret = callee_status;
}

void SIO_$K_TIMED_BREAK(int16_t *line_ptr, uint16_t *duration_ptr,
                        status_$t *status_ret)
{
    line_seen = *line_ptr;
    value_ptr_log[call_count] = duration_ptr;
    log_call(CALL_TIMED_BREAK);
    *status_ret = callee_status;
}

void KBD_$SET_KBD_MODE(short *line_ptr, unsigned char *mode, status_$t *status)
{
    line_seen = *line_ptr;
    value_byte_log[call_count] = *mode;
    log_call(CALL_SET_KBD_MODE);
    *status = callee_status;
}

void TERM_$STATUS_CONVERT(status_$t *status)
{
    log_call(CALL_STATUS_CONVERT);
    /* Mark the status so the tests can see conversion happened. */
    *status |= 0x40000000;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../control.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

static short line;
static unsigned short option;
static unsigned short value;
static status_$t status;

static void reset(void)
{
    memset(call_log, 0, sizeof(call_log));
    memset(func_num_log, 0, sizeof(func_num_log));
    memset(value_byte_log, 0, sizeof(value_byte_log));
    memset(value_ptr_log, 0, sizeof(value_ptr_log));
    memset(&param_log, 0, sizeof(param_log));
    memset(&TERM_$DATA, 0, sizeof(TERM_$DATA));
    call_count = 0;
    mask_log = 0;
    pgroup_log = NULL;
    callee_status = status_$ok;
    get_real_line_status = status_$ok;
    get_real_line_result = 0;
    line = 2;
    value = 0;
    status = 0x12345678;
}

static void run(unsigned short opt)
{
    option = opt;
    TERM_$CONTROL(&line, &option, &value, &status);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(set_func_char_arms_and_constants)
{
    static const struct { unsigned short opt; unsigned short num; } arms[] = {
        { 0, 0 }, { 1, 2 }, { 2, 3 }, { 23, 8 }, { 26, 9 }, { 28, 10 }
    };
    unsigned i;

    for (i = 0; i < sizeof(arms) / sizeof(arms[0]); i++) {
        reset();
        value = 0x4142;                 /* big-endian byte read would be 0x41 */
        run(arms[i].opt);
        ASSERT_EQ(2, call_count);
        ASSERT_EQ(CALL_SET_FUNC_CHAR, call_log[0]);
        ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);
        ASSERT_EQ(arms[i].num, func_num_log[0]);
        /* the caller's value pointer goes through untouched */
        ASSERT_EQ((unsigned long)(const void *)&value, (unsigned long)value_ptr_log[0]);
        ASSERT_EQ(2, line_seen);
    }
}

TEST(flush_set_raw_converts)
{
    reset();
    *(unsigned char *)&value = 0xFF;
    run(3);
    ASSERT_EQ(3, call_count);
    ASSERT_EQ(CALL_FLUSH_INPUT, call_log[0]);
    ASSERT_EQ(CALL_I_SET_RAW, call_log[1]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[2]);
    ASSERT_EQ(2, i_set_raw_line);
    ASSERT_EQ(0xFF, (uint8_t)i_set_raw_value);
}

TEST(invert_input_and_output_flags)
{
    reset();
    *(unsigned char *)&value = 0x0F;
    run(4);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_SET_INPUT_FLAG, call_log[0]);
    ASSERT_EQ(0, func_num_log[0]);
    ASSERT_EQ(0xF0, value_byte_log[0]);          /* not.b */
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);

    reset();
    *(unsigned char *)&value = 0xFF;
    run(5);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_SET_OUTPUT_FLAG, call_log[0]);
    ASSERT_EQ(1, func_num_log[0]);
    ASSERT_EQ(0x00, value_byte_log[0]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);
}

TEST(set_input_flag_cond_passes_pointer_through)
{
    reset();
    *(unsigned char *)&value = 0x5A;
    run(11);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_SET_INPUT_FLAG, call_log[0]);
    ASSERT_EQ(1, func_num_log[0]);
    ASSERT_EQ(0x5A, value_byte_log[0]);
    ASSERT_EQ((unsigned long)(const void *)&value, (unsigned long)value_ptr_log[0]);
}

TEST(set_output_flag_cond_uses_constant_true)
{
    reset();
    *(unsigned char *)&value = 0x00;
    run(29);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_SET_OUTPUT_FLAG, call_log[0]);
    ASSERT_EQ(1, func_num_log[0]);
    ASSERT_EQ(0xFF, value_byte_log[0]);
    ASSERT_EQ((unsigned long)(const void *)&tty_true_byte, (unsigned long)value_ptr_log[0]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);
}

TEST(enable_int_quit_two_calls)
{
    reset();
    run(8);
    ASSERT_EQ(3, call_count);
    ASSERT_EQ(CALL_ENABLE_FUNC, call_log[0]);
    ASSERT_EQ(13, func_num_log[0]);
    ASSERT_EQ(CALL_ENABLE_FUNC, call_log[1]);
    ASSERT_EQ(14, func_num_log[1]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[2]);
}

TEST(enable_func_then_own_pgroup)
{
    static const struct { unsigned short opt; unsigned short num; } arms[] = {
        { 10, 8 }, { 25, 9 }, { 27, 10 }
    };
    unsigned i;

    for (i = 0; i < 3; i++) {
        reset();
        PROC1_$AS_ID = 5;
        run(arms[i].opt);
        ASSERT_EQ(3, call_count);
        ASSERT_EQ(CALL_ENABLE_FUNC, call_log[0]);
        ASSERT_EQ(arms[i].num, func_num_log[0]);
        ASSERT_EQ(CALL_SET_PGROUP, call_log[1]);
        ASSERT_EQ((unsigned long)&PROC2_$UNWIRED_DATA.uid[5], (unsigned long)pgroup_log);
        ASSERT_EQ(CALL_STATUS_CONVERT, call_log[2]);
    }
}

TEST(set_pgroup_from_caller)
{
    reset();
    run(30);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_SET_PGROUP, call_log[0]);
    ASSERT_EQ((unsigned long)&value, (unsigned long)pgroup_log);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);
}

TEST(flush_drain_arms_convert)
{
    reset(); run(33);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_FLUSH_INPUT, call_log[0]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);

    reset(); run(34);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_FLUSH_OUTPUT, call_log[0]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);

    reset(); run(35);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_DRAIN_OUTPUT, call_log[0]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);
}

TEST(nop_arms_only_convert)
{
    reset(); run(9);
    ASSERT_EQ(1, call_count);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[0]);
    ASSERT_EQ(0x12345678 | 0x40000000, status);   /* status left as found */

    reset(); run(24);
    ASSERT_EQ(1, call_count);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[0]);
}

TEST(invalid_options_convert)
{
    unsigned short bad[] = { 14, 16, 37, 0x25, 0xFFFF };
    unsigned i;

    for (i = 0; i < 5; i++) {
        reset();
        run(bad[i]);
        ASSERT_EQ(1, call_count);
        ASSERT_EQ(CALL_STATUS_CONVERT, call_log[0]);
        ASSERT_EQ(status_$term_invalid_option | 0x40000000, status);
    }
}

TEST(set_line_flag_stores_byte_and_converts)
{
    reset();
    get_real_line_result = 1;
    *(unsigned char *)&value = 0xA5;
    run(7);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_GET_REAL_LINE, call_log[0]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);
    ASSERT_EQ(0xA5, TERM_$DATA.dtte[1].flags);
    ASSERT_EQ(0, TERM_$DATA.dtte[0].flags);
    ASSERT_EQ(0, TERM_$DATA.dtte[2].flags);
}

TEST(set_line_flag_failure_returns_raw)
{
    reset();
    get_real_line_status = 0x000b0007;
    get_real_line_result = 1;
    *(unsigned char *)&value = 0xA5;
    run(7);
    ASSERT_EQ(1, call_count);
    ASSERT_EQ(CALL_GET_REAL_LINE, call_log[0]);
    ASSERT_EQ(0x000b0007, status);                /* no conversion */
    ASSERT_EQ(0, TERM_$DATA.dtte[1].flags);
}

TEST(kbd_mode_low_byte)
{
    reset();
    value = 0x1234;
    run(36);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_SET_KBD_MODE, call_log[0]);
    ASSERT_EQ(0x34, value_byte_log[0]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);
}

TEST(timed_break)
{
    reset();
    run(22);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_TIMED_BREAK, call_log[0]);
    ASSERT_EQ((unsigned long)&value, (unsigned long)value_ptr_log[0]);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);
}

TEST(speed_doubles_word)
{
    reset();
    value = 0x2580;
    run(6);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_SET_PARAM, call_log[0]);
    ASSERT_EQ(0x25802580, param_log.baud_rate);
    ASSERT_EQ(1, mask_log);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);

    reset();
    value = 0x0960;
    run(32);
    ASSERT_EQ(0x09600960, param_log.baud_rate);
    ASSERT_EQ(2, mask_log);
}

TEST(flag_bits_and_masks)
{
    /* opt, flags1 bit, flags2 bit, mask */
    static const struct { unsigned short opt; uint32_t f1; uint32_t f2; uint32_t mask; } arms[] = {
        { 12, 0x1, 0, 0x20 }, { 13, 0x8, 0, 0x40 },
        { 31, 0, 0x1, 0x200 }, { 17, 0, 0x2, 0x400 }
    };
    unsigned i;

    for (i = 0; i < 4; i++) {
        reset();
        *(unsigned char *)&value = 0xFF;
        run(arms[i].opt);
        ASSERT_EQ(2, call_count);
        ASSERT_EQ(CALL_SET_PARAM, call_log[0]);
        ASSERT_EQ(arms[i].mask, mask_log);
        /* only the selected bit is written; the rest of the block is the
         * uninitialised local, exactly as in the image */
        ASSERT_EQ(arms[i].f1, param_log.flags1 & arms[i].f1);
        ASSERT_EQ(arms[i].f2, param_log.flags2 & arms[i].f2);
        ASSERT_EQ(CALL_STATUS_CONVERT, call_log[1]);

        reset();
        *(unsigned char *)&value = 0x7F;      /* non-negative -> bclr */
        run(arms[i].opt);
        ASSERT_EQ(0, param_log.flags1 & arms[i].f1);
        ASSERT_EQ(0, param_log.flags2 & arms[i].f2);
    }
}

TEST(enable_pgroup_sets_bit_and_pgroup)
{
    reset();
    PROC1_$AS_ID = 3;
    *(unsigned char *)&value = 0x80;
    run(15);
    ASSERT_EQ(3, call_count);
    ASSERT_EQ(CALL_SET_PGROUP, call_log[0]);
    ASSERT_EQ((unsigned long)&PROC2_$UNWIRED_DATA.uid[3], (unsigned long)pgroup_log);
    ASSERT_EQ(CALL_SET_PARAM, call_log[1]);
    ASSERT_EQ(0x4, param_log.flags2 & 0x4);
    ASSERT_EQ(0x800, mask_log);
    ASSERT_EQ(CALL_STATUS_CONVERT, call_log[2]);
}

TEST(parity_values)
{
    unsigned short good[] = { 0, 1, 3 };
    unsigned i;

    for (i = 0; i < 3; i++) {
        reset();
        value = good[i];
        run(18);
        ASSERT_EQ(2, call_count);
        ASSERT_EQ(CALL_SET_PARAM, call_log[0]);
        ASSERT_EQ(good[i], (uint16_t)param_log.parity);
        ASSERT_EQ(4, mask_log);
    }

    reset();
    value = 2;
    run(18);
    ASSERT_EQ(0, call_count);                     /* neither SET_PARAM nor convert */
    ASSERT_EQ(status_$term_invalid_option, status);
}

TEST(data_bits_values)
{
    unsigned i;

    for (i = 0; i < 4; i++) {
        reset();
        value = (unsigned short)i;
        run(19);
        ASSERT_EQ(2, call_count);
        ASSERT_EQ(CALL_SET_PARAM, call_log[0]);
        ASSERT_EQ(i, (uint16_t)param_log.char_size);
        ASSERT_EQ(0x10, mask_log);
    }

    reset();
    value = 4;
    run(19);
    ASSERT_EQ(0, call_count);
    ASSERT_EQ(status_$term_invalid_option, status);

    reset();
    value = 0x8000;                               /* bcc: unsigned compare */
    run(19);
    ASSERT_EQ(0, call_count);
    ASSERT_EQ(status_$term_invalid_option, status);
}

TEST(stop_bits_values)
{
    unsigned i;

    for (i = 1; i <= 3; i++) {
        reset();
        value = (unsigned short)i;
        run(20);
        ASSERT_EQ(2, call_count);
        ASSERT_EQ(CALL_SET_PARAM, call_log[0]);
        ASSERT_EQ(i, (uint16_t)param_log.stop_bits);
        ASSERT_EQ(8, mask_log);
    }

    reset();
    value = 0;
    run(20);
    ASSERT_EQ(0, call_count);
    ASSERT_EQ(status_$term_invalid_option, status);
}

TEST(flow_control_bit_mapping)
{
    reset();
    value = 0xF;
    run(21);
    ASSERT_EQ(2, call_count);
    ASSERT_EQ(CALL_SET_PARAM, call_log[0]);
    ASSERT_EQ(0x1B, param_log.break_mask);
    ASSERT_EQ(0x2000, mask_log);

    reset();
    value = 0x4;
    run(21);
    ASSERT_EQ(0x8, param_log.break_mask);

    reset();
    value = 0xFFF0;                               /* only bits 0..3 matter */
    run(21);
    ASSERT_EQ(0, param_log.break_mask);
}

int main(void)
{
    printf("TERM_$CONTROL tests\n");

    RUN_TEST(set_func_char_arms_and_constants);
    RUN_TEST(flush_set_raw_converts);
    RUN_TEST(invert_input_and_output_flags);
    RUN_TEST(set_input_flag_cond_passes_pointer_through);
    RUN_TEST(set_output_flag_cond_uses_constant_true);
    RUN_TEST(enable_int_quit_two_calls);
    RUN_TEST(enable_func_then_own_pgroup);
    RUN_TEST(set_pgroup_from_caller);
    RUN_TEST(flush_drain_arms_convert);
    RUN_TEST(nop_arms_only_convert);
    RUN_TEST(invalid_options_convert);
    RUN_TEST(set_line_flag_stores_byte_and_converts);
    RUN_TEST(set_line_flag_failure_returns_raw);
    RUN_TEST(kbd_mode_low_byte);
    RUN_TEST(timed_break);
    RUN_TEST(speed_doubles_word);
    RUN_TEST(flag_bits_and_masks);
    RUN_TEST(enable_pgroup_sets_bit_and_pgroup);
    RUN_TEST(parity_values);
    RUN_TEST(data_bits_values);
    RUN_TEST(stop_bits_values);
    RUN_TEST(flow_control_bit_mapping);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
