/*
 * ring/test/test_close_os.c - RING_$CLOSE_OS (0x00E77C24)
 *
 * Pins the argument forwarding to RING_$SVC_CLOSE (the unit by the address
 * of its own slot, a COPY of the channel, word 0) and the status copy-out.
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

#include "ring/ring_internal.h"

static uint16_t seen_unit, seen_ch;
static int16_t seen_w;
static int ch_is_copy;
static uint16_t *caller_ch;

void RING_$SVC_CLOSE(uint16_t *unit_ptr, uint16_t *channel_ptr,
                     int16_t unused3, uint32_t unused4, status_$t *status_ret)
{
    (void)unused4;
    seen_unit = *unit_ptr;
    seen_ch = *channel_ptr;
    seen_w = unused3;
    ch_is_copy = (channel_ptr != caller_ch);
    *status_ret = 0x0031000A;
}

#include "../close_os.c"

TEST(forwards_and_copies_status)
{
    uint16_t ch = 7;
    status_$t st = 0;
    caller_ch = &ch;
    RING_$CLOSE_OS(1, &ch, &st);
    ASSERT_EQ(1, seen_unit);
    ASSERT_EQ(7, seen_ch);
    ASSERT_EQ(0, seen_w);
    ASSERT_EQ(1, ch_is_copy);
    ASSERT_EQ(0x0031000A, st);
}

int main(void)
{
    printf("RING_$CLOSE_OS tests:\n");
    RUN_TEST(forwards_and_copies_status);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
