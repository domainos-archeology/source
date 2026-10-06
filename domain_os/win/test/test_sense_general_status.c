/*
 * win/test/test_sense_general_status.c - win_$sense_general_status
 * (0x00E19186): the two ANSI reports, the counters, the status priority.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
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

#include "win/win_internal.h"

MODULE_DATA_DEFINE(win_$data_t, WIN_$DATA, 0x00E2B89C);

static uint8_t reply_0e, reply_0d;
static status_$t ansi_status_0e, ansi_status_0d;
static uint16_t cmds[4];
static int ncmds;
status_$t WIN_$ANSI_COMMAND(uint16_t unit, uint16_t cmd, char *in, char *out)
{
    (void)unit; (void)in;
    cmds[ncmds++] = cmd;
    if (cmd == 0x0E) {
        if (out != (char *)&WIN_DATA_BASE[0x70]) { tests_failed++; }
        *out = (char)reply_0e;
        return ansi_status_0e;
    }
    if (out != (char *)&WIN_DATA_BASE[0x71]) { tests_failed++; }
    *out = (char)reply_0d;
    return ansi_status_0d;
}

#include "../sense_general_status.c"

static uint16_t out;

static void setup(void)
{
    memset(&WIN_$DATA, 0, sizeof(WIN_$DATA));
    WIN_DATA_BASE[0x70] = 0xAA;
    WIN_DATA_BASE[0x71] = 0xAA;
    reply_0e = reply_0d = 0;
    ansi_status_0e = ansi_status_0d = 0;
    ncmds = 0;
    out = 0x5555;
}

TEST(nothing_to_sense)
{
    setup();
    ASSERT_EQ(0, win_$sense_general_status(0, 0xCF, &out));
    ASSERT_EQ(0, out);
    ASSERT_EQ(0, ncmds);
    ASSERT_EQ(0xAA, WIN_DATA_BASE[0x70]);       /* not cleared */
}

TEST(report_a_mappings)
{
    setup();
    reply_0e = 0x01;
    ASSERT_EQ(status_$disk_seek_error, win_$sense_general_status(0, 0x10, &out));
    ASSERT_EQ(1, out);
    ASSERT_EQ(1, *(uint16_t *)&WIN_DATA_BASE[0x4C]);
    ASSERT_EQ(0, WIN_DATA_BASE[0x71]);          /* cleared with 0x70 */
    setup();
    reply_0e = 0x08;
    ASSERT_EQ(status_$disk_write_protected, win_$sense_general_status(0, 0x10, &out));
    setup();
    reply_0e = 0x04;
    ASSERT_EQ(status_$disk_equipment_check, win_$sense_general_status(0, 0x10, &out));
    ASSERT_EQ(1, *(uint16_t *)&WIN_DATA_BASE[0x4E]);
    setup();
    reply_0e = 0x40;
    ASSERT_EQ(status_$unknown_error_status_from_drive,
              win_$sense_general_status(0, 0x10, &out));
    setup();
    ansi_status_0e = 0x00080099;
    ASSERT_EQ(0x00080099, win_$sense_general_status(0, 0x10, &out));
}

TEST(report_b_mappings)
{
    setup();
    reply_0d = 0x02;
    ASSERT_EQ(status_$disk_not_ready, win_$sense_general_status(0, 0x21, &out));
    ASSERT_EQ(2, out);
    setup();
    reply_0d = 0x02;                /* general bit 0 clear, report bit 0 clear */
    ASSERT_EQ(status_$unknown_error_status_from_drive,
              win_$sense_general_status(0, 0x20, &out));
    setup();
    reply_0d = 0x03;
    ASSERT_EQ(0, win_$sense_general_status(0, 0x20, &out));
}

TEST(both_bits_first_status_wins)
{
    setup();
    reply_0e = 0x08;
    reply_0d = 0x02;
    ASSERT_EQ(status_$disk_write_protected, win_$sense_general_status(0, 0x31, &out));
    ASSERT_EQ(2, out);
    ASSERT_EQ(2, ncmds);
    ASSERT_EQ(0x0E, cmds[0]);
    ASSERT_EQ(0x0D, cmds[1]);
    setup();
    ansi_status_0e = 0;
    reply_0e = 0;                   /* unknown from A ... */
    reply_0d = 0x01;                /* ... B fine: A's status still wins */
    ASSERT_EQ(status_$unknown_error_status_from_drive,
              win_$sense_general_status(0, 0x30, &out));
}

int main(void)
{
    printf("win_$sense_general_status\n");
    RUN_TEST(nothing_to_sense);
    RUN_TEST(report_a_mappings);
    RUN_TEST(report_b_mappings);
    RUN_TEST(both_bits_first_status_wins);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
