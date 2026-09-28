/*
 * flp/test/test_shake.c - SHAKE (0x00E3E49E)
 *
 * The FDC register block is a plain struct inside a VA arena the host
 * ARCH_VA_TO_PTR maps, so FLP_DATA.hw_addr can be a 32-bit offset into it.
 * A static status byte is enough for every path: RQM set means "ready at
 * once", RQM clear means "never ready" (the 2000-poll timeout).
 */

#include <stdio.h>
#include <string.h>

#include "flp/flp_internal.h"

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
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Code under test and its data
 * ========================================================================== */

#include "../flp_data.c"
#include "../shake.c"

/* The VA arena: the register block sits at offset 0x100. */
static uint8_t va_arena[0x400];
#define REGS_VA 0x100
#define REGS ((volatile flp_regs_t *)(va_arena + REGS_VA))

static void reset(void)
{
    memset(va_arena, 0, sizeof(va_arena));
    FLP_DATA.hw_addr = REGS_VA;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E3E4B8-0x00E3E4BC: count 0 -> status_$ok without touching the FDC. */
TEST(zero_count_is_ok)
{
    uint16_t buf[1] = { 0x1234 };
    int16_t count = 0;
    int16_t dir = 1;

    reset();
    REGS->status = 0;           /* would time out if polled */
    ASSERT_EQ(status_$ok, SHAKE(buf, &count, &dir));
    ASSERT_EQ(0x1234, buf[0]);
}

/* 0x00E3E4E8-0x00E3E4F4: three reads, each byte stored as a word. */
TEST(read_three_words)
{
    uint16_t buf[3] = { 0xFFFF, 0xFFFF, 0xFFFF };
    int16_t count = 3;
    int16_t dir = 0;

    reset();
    REGS->status = FLP_STATUS_RQM | FLP_STATUS_DIO;
    REGS->data = 0xA5;
    ASSERT_EQ(status_$ok, SHAKE(buf, &count, &dir));
    ASSERT_EQ(0x00A5, buf[0]);
    ASSERT_EQ(0x00A5, buf[1]);
    ASSERT_EQ(0x00A5, buf[2]);
}

/* 0x00E3E4F6-0x00E3E502: a write sends the LOW byte of each word. */
TEST(write_sends_low_byte)
{
    uint16_t buf[2] = { 0x1203, 0x34DF };
    int16_t count = 2;
    int16_t dir = 1;

    reset();
    REGS->status = FLP_STATUS_RQM;
    ASSERT_EQ(status_$ok, SHAKE(buf, &count, &dir));
    ASSERT_EQ(0xDF, REGS->data);        /* the last word's low byte */
}

/* 0x00E3E4E8-0x00E3E4EA: DIO set while writing. */
TEST(fdc_wants_to_talk_during_a_write)
{
    uint16_t buf[1] = { 0x0008 };
    int16_t count = 1;
    int16_t dir = 1;

    reset();
    REGS->status = FLP_STATUS_RQM | FLP_STATUS_DIO;
    ASSERT_EQ(status_$disk_controller_error, SHAKE(buf, &count, &dir));
}

/* 0x00E3E4F6-0x00E3E4FA: DIO clear while reading. */
TEST(fdc_wants_to_listen_during_a_read)
{
    uint16_t buf[1] = { 0 };
    int16_t count = 1;
    int16_t dir = 0;

    reset();
    REGS->status = FLP_STATUS_RQM;
    ASSERT_EQ(status_$disk_controller_error, SHAKE(buf, &count, &dir));
}

/* 0x00E3E4CC-0x00E3E4D8: RQM never comes. */
TEST(no_rqm_times_out)
{
    uint16_t buf[1] = { 0 };
    int16_t count = 1;
    int16_t dir = 0;

    reset();
    REGS->status = 0;
    ASSERT_EQ(status_$disk_controller_timeout, SHAKE(buf, &count, &dir));
}

/* A direction word other than 0 or 1 matches neither branch. */
TEST(direction_two_is_always_an_error)
{
    uint16_t buf[1] = { 0 };
    int16_t count = 1;
    int16_t dir = 2;

    reset();
    REGS->status = FLP_STATUS_RQM;
    ASSERT_EQ(status_$disk_controller_error, SHAKE(buf, &count, &dir));
    REGS->status = FLP_STATUS_RQM | FLP_STATUS_DIO;
    ASSERT_EQ(status_$disk_controller_error, SHAKE(buf, &count, &dir));
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;

    printf("SHAKE tests\n");
    RUN_TEST(zero_count_is_ok);
    RUN_TEST(read_three_words);
    RUN_TEST(write_sends_low_byte);
    RUN_TEST(fdc_wants_to_talk_during_a_write);
    RUN_TEST(fdc_wants_to_listen_during_a_read);
    RUN_TEST(no_rqm_times_out);
    RUN_TEST(direction_two_is_always_an_error);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
