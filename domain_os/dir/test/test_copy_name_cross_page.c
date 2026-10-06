/*
 * dir/test/test_copy_name_cross_page.c - dir_$copy_name_cross_page
 * (0x00E4F034)
 *
 * Pins: bytes move from src_page+src_offset to dest_page+dest_offset in
 * chunks of at most 0x20, each chunk mapping the source then the
 * destination page; a zero count still maps both once and copies nothing.
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

static uint8_t pages[4][0x400];
static int nmap;
static int16_t map_log[16];

void *dir_$map_page(void *handle, int16_t page_idx)
{
    (void)handle;
    if (nmap < 16) map_log[nmap] = page_idx;
    nmap++;
    return pages[page_idx];
}

#include "../copy_name_cross_page.c"

static void setup(void)
{
    int i;
    memset(pages, 0, sizeof(pages));
    for (i = 0; i < 0x400; i++) pages[1][i] = (uint8_t)(i * 7);
    nmap = 0;
}

TEST(long_name_in_chunks)
{
    setup();
    dir_$copy_name_cross_page(0x100, 1, 0x40, 2, 0x300, 0x45);
    ASSERT_EQ(0, memcmp(pages[2] + 0x300, pages[1] + 0x40, 0x45));
    ASSERT_EQ(0, pages[2][0x345]);
    ASSERT_EQ(6, nmap);                 /* 0x20, 0x20, 0x05 */
    ASSERT_EQ(1, map_log[0]);
    ASSERT_EQ(2, map_log[1]);
    ASSERT_EQ(1, map_log[4]);
}

TEST(zero_count)
{
    setup();
    dir_$copy_name_cross_page(0x100, 1, 0x40, 2, 0x300, 0);
    ASSERT_EQ(2, nmap);
    ASSERT_EQ(0, pages[2][0x300]);
}

int main(void)
{
    printf("dir_$copy_name_cross_page tests\n");
    RUN_TEST(long_name_in_chunks);
    RUN_TEST(zero_count);
    TEST_SUMMARY();
}
