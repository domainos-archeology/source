/*
 * dir/test/test_old_find_free_inline_slot.c - dir_$old_find_free_inline_slot
 * (0x00E54DCC)
 *
 * Pins: the first inline slot whose type byte (dir + 0x30*i + 0x11) is 0 is
 * returned 1-based with TRUE; none free or no slots gives FALSE and leaves
 * slot_out alone.
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

#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed); \
    return tests_failed ? 1 : 0; \
} while (0)

#include "dir/dir_internal.h"
#include "../old_find_free_inline_slot.c"

static uint8_t dirbuf[0x400] __attribute__((aligned(4)));

static uint32_t setup(void)
{
    memset(dirbuf, 0, sizeof(dirbuf));
    ARCH_HOST_VA_BASE = (uintptr_t)dirbuf - 0x100;
    return 0x100;
}

TEST(finds_first_free)
{
    uint32_t h = setup();
    uint16_t slot = 0xAAAA;
    dirbuf[0x30 * 1 + 0x11] = 1;
    dirbuf[0x30 * 2 + 0x11] = 3;
    ASSERT_EQ(0xFF, (uint8_t)dir_$old_find_free_inline_slot(h, 4, &slot));
    ASSERT_EQ(3, slot);
}

TEST(all_used)
{
    uint32_t h = setup();
    uint16_t slot = 0xAAAA;
    dirbuf[0x30 * 1 + 0x11] = 1;
    dirbuf[0x30 * 2 + 0x11] = 1;
    ASSERT_EQ(0, dir_$old_find_free_inline_slot(h, 2, &slot));
    ASSERT_EQ(0xAAAA, slot);
}

TEST(no_slots)
{
    uint32_t h = setup();
    uint16_t slot = 0xAAAA;
    ASSERT_EQ(0, dir_$old_find_free_inline_slot(h, 0, &slot));
    ASSERT_EQ(0xAAAA, slot);
}

int main(void)
{
    printf("dir_$old_find_free_inline_slot tests\n");
    RUN_TEST(finds_first_free);
    RUN_TEST(all_used);
    RUN_TEST(no_slots);
    TEST_SUMMARY();
}
