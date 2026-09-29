/*
 * ast/test/test_init.c - Unit tests for AST_$INIT (0x00E2F000)
 *
 * The test #includes ast/init.c directly and drives the real routine
 * through mocked AST_$ADD_AOTES / AST_$ADD_ASTES / CRASH_SYSTEM.  It pins
 * the sizing arithmetic against hand-evaluated values of the listing:
 *
 *   blocks = (REAL_PAGES + 0x1FF) >> 9
 *   astes  = blocks*0x50 + 0x280            (clamped to 0x1F8)
 *   aotes  = (((blocks*0x28*0xC0 + 0x3FF) >> 10) << 10) / 0xC0  (clamped to 0x118)
 *
 * and the crash on a failed add.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/init.c"

MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);

static uint16_t  add_aotes_count;
static status_$t add_aotes_status;
static int       add_aotes_calls;
uint16_t AST_$ADD_AOTES(uint16_t *count, status_$t *status)
{
    add_aotes_calls++;
    add_aotes_count = *count;
    *status = add_aotes_status;
    return *count;
}

static uint16_t  add_astes_count;
static status_$t add_astes_status;
static int       add_astes_calls;
uint16_t AST_$ADD_ASTES(uint16_t *count, status_$t *status)
{
    add_astes_calls++;
    add_astes_count = *count;
    *status = add_astes_status;
    return *count;
}

static int crash_calls;
void CRASH_SYSTEM(const status_$t *status_p) { (void)status_p; crash_calls++; }

static void reset_state(void)
{
    memset(&MMAP_$DATA, 0, sizeof(MMAP_$DATA));
    add_aotes_calls = add_astes_calls = 0;
    add_aotes_count = add_astes_count = 0;
    add_aotes_status = add_astes_status = status_$ok;
    crash_calls = 0;
}

/* 1024 real pages: blocks = 2; astes = 0x320, which is already above the
 * 0x1F8 clamp (0x280 alone is - the ASTE count is ALWAYS 0x1F8 on this
 * image); aotes: 80*0xC0 = 0x3C00, +0x3FF >> 10 = 0xF, << 10 = 0x3C00,
 * / 0xC0 = 80 */
TEST(one_megabyte)
{
    MMAP_$REAL_PAGES = 1024;

    AST_$INIT();

    ASSERT_EQ(1, add_aotes_calls);
    ASSERT_EQ(80, add_aotes_count);
    ASSERT_EQ(1, add_astes_calls);
    ASSERT_EQ(AST_MAX_ASTE, add_astes_count);
    ASSERT_EQ(0, crash_calls);
}

/* 1500 pages: blocks = (1500+511)>>9 = 3; astes = 0x370 -> 0x1F8; aotes:
 * 120*0xC0 = 0x5A00, +0x3FF = 0x5DFF >> 10 = 0x17, << 10 = 0x5C00,
 * / 0xC0 = 122 */
TEST(rounding_up_to_pages)
{
    MMAP_$REAL_PAGES = 1500;

    AST_$INIT();

    ASSERT_EQ(122, add_aotes_count);
    ASSERT_EQ(AST_MAX_ASTE, add_astes_count);
}

/* 8192 pages: blocks = 16; astes = 0x780 -> 0x1F8; aotes: 640*0xC0 =
 * 0x1E000, +0x3FF >> 10 = 0x78, << 10 = 0x1E000, / 0xC0 = 640 -> 0x118 */
TEST(clamped_to_maxima)
{
    MMAP_$REAL_PAGES = 8192;

    AST_$INIT();

    ASSERT_EQ(AST_MAX_AOTE, add_aotes_count);
    ASSERT_EQ(AST_MAX_ASTE, add_astes_count);
}

TEST(failed_add_crashes)
{
    MMAP_$REAL_PAGES = 1024;
    add_aotes_status = status_$ast_incompatible_request;
    add_astes_status = status_$ast_incompatible_request;

    AST_$INIT();

    ASSERT_EQ(2, crash_calls);
    ASSERT_EQ(1, add_astes_calls);              /* the mock returns; both run */
}

int main(void)
{
    printf("test_init (AST_$INIT 0x00E2F000)\n");

    RUN_TEST(one_megabyte);
    RUN_TEST(rounding_up_to_pages);
    RUN_TEST(clamped_to_maxima);
    RUN_TEST(failed_add_crashes);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
