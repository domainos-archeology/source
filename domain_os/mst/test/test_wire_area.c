/*
 * mst/test/test_wire_area.c - unit tests for MST_$WIRE_AREA (0x00E44BA4)
 *
 * MST_$WIRE and CRASH_SYSTEM are mocked; the page list is a host array
 * filled 1-based through the (-4, A2, count*4) addressing.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mst/mst_internal.h"
#include "misc/crash_system.h"

/* ---- mocks ----------------------------------------------------------- */

static int wire_calls;
static uint32_t wire_va[16];
static status_$t wire_status;
static int crash_calls;
static status_$t crash_status[16];

uint32_t MST_$WIRE(uint32_t vpn, status_$t *status_ret)
{
    wire_va[wire_calls & 15] = vpn;
    wire_calls++;
    *status_ret = wire_status;
    return 0x100 + (vpn >> 10);
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_status[crash_calls & 15] = *status_p;
    crash_calls++;
}

static void reset_state(void)
{
    wire_calls = 0;
    wire_status = status_$ok;
    crash_calls = 0;
    memset(wire_va, 0, sizeof(wire_va));
    memset(crash_status, 0, sizeof(crash_status));
}

#include "../wire_area.c"

/* ---- tests ----------------------------------------------------------- */

static void test_wires_each_page(void)
{
    uint32_t start = 0x8000, end = 0x8BFF;      /* pages 0x20..0x22 */
    int16_t max = 8, count = 77;
    uint32_t list[8] = {0};

    MST_$WIRE_AREA(&start, &end, list, &max, &count);
    ASSERT_EQ(3, count);
    ASSERT_EQ(3, wire_calls);
    ASSERT_EQ(0x8000, wire_va[0]);
    ASSERT_EQ(0x8400, wire_va[1]);
    ASSERT_EQ(0x8800, wire_va[2]);
    ASSERT_EQ(0x120, list[0]);
    ASSERT_EQ(0x122, list[2]);
    ASSERT_EQ(0, list[3]);
    ASSERT_EQ(0, crash_calls);
}

static void test_unaligned_start_counts_pages(void)
{
    uint32_t start = 0x83FF, end = 0x8400;      /* two pages */
    int16_t max = 8, count = 0;
    uint32_t list[8] = {0};

    MST_$WIRE_AREA(&start, &end, list, &max, &count);
    ASSERT_EQ(2, count);
    /* the VA steps by 0x400 from the unaligned start */
    ASSERT_EQ(0x87FF, wire_va[1]);
}

static void test_end_one_page_below_start_does_nothing(void)
{
    uint32_t start = 0x8400, end = 0x8000;      /* n = 0 */
    int16_t max = 8, count = 5;
    uint32_t list[8] = {0};

    MST_$WIRE_AREA(&start, &end, list, &max, &count);
    ASSERT_EQ(0, count);
    ASSERT_EQ(0, wire_calls);
}

static void test_overflow_crashes_and_continues(void)
{
    uint32_t start = 0, end = 0x0BFF;           /* three pages */
    int16_t max = 2, count = 0;
    uint32_t list[8] = {0};

    MST_$WIRE_AREA(&start, &end, list, &max, &count);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x0004000C, crash_status[0]);
    ASSERT_EQ(3, wire_calls);
    ASSERT_EQ(0x102, list[2]);
}

static void test_wire_failure_crashes_with_status(void)
{
    uint32_t start = 0, end = 0;
    int16_t max = 2, count = 0;
    uint32_t list[2] = {0};

    wire_status = 0x00050004;
    MST_$WIRE_AREA(&start, &end, list, &max, &count);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00050004, crash_status[0]);
    ASSERT_EQ(0x100, list[0]);
}

int main(void)
{
    printf("MST_$WIRE_AREA tests:\n");
    RUN_TEST(wires_each_page);
    RUN_TEST(unaligned_start_counts_pages);
    RUN_TEST(end_one_page_below_start_does_nothing);
    RUN_TEST(overflow_crashes_and_continues);
    RUN_TEST(wire_failure_crashes_with_status);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
