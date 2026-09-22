/*
 * disk/test/test_spin_down.c - Unit tests for DISK_$SPIN_DOWN (0x00E3DB04)
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "time/time.h"

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

disk_device_entry_t DISK_$DEVICES[DISK_MAX_DEVICES];

static int waits;
static uint16_t wait_type;
static clock_t wait_clock;
void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    waits++;
    wait_type = *delay_type;
    wait_clock = *delay;
    *status = 0;
}

#include "../spin_down.c"

static int16_t times[DISK_MAX_DEVICES];
static uint16_t *seen_ctrl[DISK_MAX_DEVICES];
static int calls;

static int16_t mock_spin(uint16_t *ctrl)
{
    seen_ctrl[calls] = ctrl;
    return times[calls++];
}

static disk_jump_table_t jt_with, jt_without;

static void reset(void)
{
    memset(DISK_$DEVICES, 0, sizeof DISK_$DEVICES);
    memset(&jt_with, 0, sizeof jt_with);
    memset(&jt_without, 0, sizeof jt_without);
    jt_with.spin_down = mock_spin;
    calls = waits = 0;
}

TEST(waits_for_the_slowest)
{
    reset();
    DISK_$DEVICES[1].jump_table = &jt_with;
    DISK_$DEVICES[1].controller = 7;
    DISK_$DEVICES[3].jump_table = &jt_without;
    DISK_$DEVICES[5].jump_table = &jt_with;
    times[0] = 2; times[1] = 9;
    DISK_$SPIN_DOWN();
    ASSERT_EQ(2, calls);
    ASSERT_EQ((unsigned long)&DISK_$DEVICES[1].controller, (unsigned long)seen_ctrl[0]);
    ASSERT_EQ(1, waits);
    ASSERT_EQ(0, wait_type);
    ASSERT_EQ(9 << 2, wait_clock.high);
    ASSERT_EQ(0, wait_clock.low);
}

TEST(no_wait_when_nothing_positive)
{
    reset();
    DISK_$DEVICES[0].jump_table = &jt_with;
    times[0] = -3;
    DISK_$SPIN_DOWN();
    ASSERT_EQ(1, calls);
    ASSERT_EQ(0, waits);
}

TEST(empty_table)
{
    reset();
    DISK_$SPIN_DOWN();
    ASSERT_EQ(0, calls);
    ASSERT_EQ(0, waits);
}

int main(void)
{
    printf("test_spin_down:\n");
    RUN_TEST(waits_for_the_slowest);
    RUN_TEST(no_wait_when_nothing_positive);
    RUN_TEST(empty_table);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
