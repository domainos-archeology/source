/*
 * term/test/test_get_ec.c - Unit tests for TERM_$GET_REAL_LINE (0x00E1A9D4),
 * TERM_$GET_EC (0x00E723C0) and TERM_$HELP_CALLBACK (0x00E7244E)
 *
 * The real .c files are #included; EC2_$REGISTER_EC1 is mocked.
 */

#include <stdio.h>
#include <string.h>

#include "term/term_internal.h"

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-44s %s\n", #name, current_failed ? "FAIL" : "ok");          \
    } while (0)
#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long e_ = (long long)(expected), a_ = (long long)(actual);       \
        if (e_ != a_) {                                                       \
            printf("  %s:%d: expected 0x%llx, got 0x%llx\n",                  \
                   __FILE__, __LINE__, (unsigned long long)e_,                \
                   (unsigned long long)a_);                                   \
            if (!current_failed) { current_failed = 1; tests_failed++; }      \
        }                                                                     \
    } while (0)
#define ASSERT_PTR_EQ(e, a) ASSERT_EQ((uintptr_t)(e), (uintptr_t)(a))

term_data_t TERM_$DATA;
uint16_t PROC1_$CURRENT;
uint16_t DTTY_$CTRL;

static int reg_calls;
static ec_$eventcount_t *reg_ec;
static status_$t reg_status_value;
static uint8_t handle_cell;
void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret)
{
    reg_calls++;
    reg_ec = ec1;
    *status_ret = reg_status_value;
    return &handle_cell;
}

#include "../get_real_line.c"
#include "../get_ec.c"
#include "../help_callback.c"

static void reset(void)
{
    /* handle_cell sits at target VA 0x1000 so a host pointer round-trips
     * through the 32-bit cell TERM_$GET_EC stores into */
    ARCH_HOST_VA_BASE = (uintptr_t)&handle_cell - 0x1000;
    memset(&TERM_$DATA, 0, sizeof(TERM_$DATA));
    TERM_$MAX_DTTE = 3;
    PROC1_$CURRENT = 5;
    DTTY_$CTRL = 2;
    reg_calls = 0;
    reg_status_value = status_$ok;
}

TEST(real_line_mapping)
{
    status_$t st;
    reset();
    ASSERT_EQ(2, TERM_$GET_REAL_LINE(0, &st));      /* -> DTTY_$CTRL */
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, TERM_$GET_REAL_LINE(1, &st));      /* not process 1 */
    ASSERT_EQ(status_$ok, st);
    PROC1_$CURRENT = 1;
    ASSERT_EQ(2, TERM_$GET_REAL_LINE(1, &st));      /* process 1: console */
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(2, TERM_$GET_REAL_LINE(2, &st));
    ASSERT_EQ(status_$ok, st);
}

TEST(real_line_errors)
{
    status_$t st;
    reset();
    st = 0;
    ASSERT_EQ(4, TERM_$GET_REAL_LINE(4, &st));
    ASSERT_EQ(status_$invalid_line_number, st);
    st = 0;
    ASSERT_EQ((short)-1, TERM_$GET_REAL_LINE(-1, &st));   /* unsigned > 3 */
    ASSERT_EQ(status_$invalid_line_number, st);
    st = 0;
    ASSERT_EQ(3, TERM_$GET_REAL_LINE(3, &st));      /* 3 >= MAX_DTTE 3 */
    ASSERT_EQ(status_$requested_line_or_operation_not_implemented, st);
    DTTY_$CTRL = 3;
    st = 0;
    ASSERT_EQ(3, TERM_$GET_REAL_LINE(0, &st));      /* mapped value is checked */
    ASSERT_EQ(status_$requested_line_or_operation_not_implemented, st);
}

TEST(get_ec_input_and_output)
{
    unsigned short id;
    short line = 2;
    ec2_$eventcount_t out;
    status_$t st = 0x55;

    reset();
    id = 0;
    memset(&out, 0, sizeof(out));
    TERM_$GET_EC(&id, &line, &out, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, reg_calls);
    ASSERT_PTR_EQ((uint8_t *)&TERM_$DATA.dtte[2] + 0x0C, reg_ec);
    /* the cell receives the handle's 32-bit VA (move.l A0,(A1) at 0x00E72442) */
    ASSERT_EQ(ARCH_PTR_TO_VA(&handle_cell), *(m68k_ptr_t *)&out);
    ASSERT_EQ(0x1000, *(m68k_ptr_t *)&out);

    id = 1;
    TERM_$GET_EC(&id, &line, &out, &st);
    ASSERT_EQ(2, reg_calls);
    ASSERT_PTR_EQ((uint8_t *)&TERM_$DATA.dtte[2] + 0x18, reg_ec);
}

TEST(get_ec_errors)
{
    unsigned short id = 2;
    short line = 0;
    ec2_$eventcount_t out;
    status_$t st = 0;

    reset();
    TERM_$GET_EC(&id, &line, &out, &st);
    ASSERT_EQ(status_$term_invalid_option, st);
    ASSERT_EQ(0x000B0004, st);
    ASSERT_EQ(0, reg_calls);

    id = 0; line = 7; st = 0;
    TERM_$GET_EC(&id, &line, &out, &st);
    ASSERT_EQ(status_$invalid_line_number, st);
    ASSERT_EQ(0, reg_calls);

    id = 0; line = 0; reg_status_value = 0x00120001;
    TERM_$GET_EC(&id, &line, &out, &st);
    ASSERT_EQ(0x00120001, st);
    ASSERT_EQ(1, reg_calls);

    TERM_$HELP_CALLBACK();
}

int main(void)
{
    RUN_TEST(real_line_mapping);
    RUN_TEST(real_line_errors);
    RUN_TEST(get_ec_input_and_output);
    RUN_TEST(get_ec_errors);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
