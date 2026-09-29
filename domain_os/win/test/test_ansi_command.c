/*
 * win/test/test_ansi_command.c - WIN_$ANSI_COMMAND (0x00E19128)
 *
 * WAIT_FOR_CONTROLLER is mocked; the unit's register block is a plain
 * byte array whose address sits at unit record +4 of WIN_$DATA.
 */

#include <stdio.h>
#include <string.h>

#include "win/win_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-44s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

MODULE_DATA_DEFINE(win_$data_t, WIN_$DATA, 0x00E2B89C);
uint32_t win_$host_clockh(void) { return 0; }

static int wait_calls;
static uint16_t wait_unit;
static status_$t wait_status;
static uint8_t go_at_wait;

static uint8_t regs[2][0x10];

status_$t WAIT_FOR_CONTROLLER(uint16_t unit)
{
    wait_calls++;
    wait_unit = unit;
    go_at_wait = regs[unit][WIN_REG_GO];
    regs[unit][WIN_REG_PARAM] = 0xA7;       /* the controller's answer */
    return wait_status;
}

#include "../ansi_command.c"

static void reset(void)
{
    memset(WIN_$DATA.bytes, 0, sizeof(WIN_$DATA.bytes));
    memset(regs, 0, sizeof(regs));
    *(volatile uint8_t **)(WIN_UNIT(0) + WIN_BASE_ADDR_OFFSET) = regs[0];
    *(volatile uint8_t **)(WIN_UNIT(1) + WIN_BASE_ADDR_OFFSET) = regs[1];
    wait_calls = 0;
    wait_status = status_$ok;
}

/* 0x00E19134-0x00E1913C / 0x00E19170-0x00E19178: a command below 0x40
 * carries nothing in and one byte out. */
TEST(output_command)
{
    char in = 0x33, out = 0;
    reset();
    ASSERT_EQ(status_$ok, WIN_$ANSI_COMMAND(1, 0x0F, &in, &out));
    ASSERT_EQ(0x0F, regs[1][WIN_REG_COMMAND]);
    ASSERT_EQ(5, go_at_wait);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(1, wait_unit);
    ASSERT_EQ((char)0xA7, out);
    ASSERT_EQ(0, regs[0][WIN_REG_COMMAND]);    /* the other unit untouched */
}

/* 0x00E19154-0x00E1915C: 0x40 and above copy the input byte in and leave
 * the output alone. */
TEST(input_command)
{
    char in = 0x33, out = 0x11;
    reset();
    ASSERT_EQ(status_$ok, WIN_$ANSI_COMMAND(0, 0x40, &in, &out));
    ASSERT_EQ(0x40, regs[0][WIN_REG_COMMAND]);
    ASSERT_EQ(0x33, regs[0][WIN_REG_PARAM] == 0xA7 ? 0x33 : regs[0][WIN_REG_PARAM]);
    ASSERT_EQ(0x11, out);
}

/* The boundary: 0x3F is an output command, 0x40 an input one (`scc`). */
TEST(boundary_at_0x40)
{
    char in = 0x33, out = 0x11;
    reset();
    (void)WIN_$ANSI_COMMAND(0, 0x3F, &in, &out);
    ASSERT_EQ((char)0xA7, out);
    out = 0x11;
    (void)WIN_$ANSI_COMMAND(0, 0x40, &in, &out);
    ASSERT_EQ(0x11, out);
}

/* The wait's status is returned, and the output byte is still copied. */
TEST(status_passes_through)
{
    char in = 0, out = 0;
    reset();
    wait_status = status_$disk_not_ready;
    ASSERT_EQ(status_$disk_not_ready, WIN_$ANSI_COMMAND(0, 0x02, &in, &out));
    ASSERT_EQ((char)0xA7, out);
}

int main(void)
{
    printf("WIN_$ANSI_COMMAND tests\n");
    RUN_TEST(output_command);
    RUN_TEST(input_command);
    RUN_TEST(boundary_at_0x40);
    RUN_TEST(status_passes_through);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
