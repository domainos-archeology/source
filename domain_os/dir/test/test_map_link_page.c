/*
 * dir/test/test_map_link_page.c - dir_$map_link_page (0x00E4D572)
 *
 * Pins: the page comes from dir_$map_page(handle, page); a page whose kind
 * bits (byte 0, bits 7..6) are 2 yields page + 1; any other kind crashes
 * with Naming_bad_request_header_ver_err.
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

status_$t Naming_bad_request_header_ver_err = 0x000E0025;
static uint8_t page[0x400];
static int16_t page_seen;
static void *handle_seen;
static int ncrash;
static const status_$t *crash_arg;

void *dir_$map_page(void *handle, int16_t page_idx)
{
    handle_seen = handle;
    page_seen = page_idx;
    return page;
}
void CRASH_SYSTEM(const status_$t *status) { ncrash++; crash_arg = status; }

#include "../map_link_page.c"

TEST(link_page)
{
    uint8_t *p;
    memset(page, 0, sizeof(page));
    page[0] = 0x80;
    ncrash = 0;
    p = (uint8_t *)dir_$map_link_page((void *)0x1234, 7);
    ASSERT_EQ(7, page_seen);
    ASSERT_EQ(0x1234, (uintptr_t)handle_seen);
    ASSERT_EQ(1, p == page + 1);
    ASSERT_EQ(0, ncrash);
}

TEST(wrong_kind_crashes)
{
    uint8_t *p;
    memset(page, 0, sizeof(page));
    page[0] = 0x40;
    ncrash = 0;
    p = (uint8_t *)dir_$map_link_page((void *)0x1234, 3);
    ASSERT_EQ(1, ncrash);
    ASSERT_EQ(1, crash_arg == &Naming_bad_request_header_ver_err);
    ASSERT_EQ(1, p == page + 1);
}

int main(void)
{
    printf("dir_$map_link_page tests\n");
    RUN_TEST(link_page);
    RUN_TEST(wrong_kind_crashes);
    TEST_SUMMARY();
}
