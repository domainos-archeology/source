/*
 * dir/test/test_finalize_split.c - Unit tests for dir_$finalize_split
 *
 * Tests the B-tree split finalization. We mock dir_$purify_split_pages
 * and dir_$truncate_pages, then verify that:
 *   - The saved page count is captured before the copy loop
 *   - path_page[] values are correctly copied into split_pages[]
 *   - purify_split_pages is called
 *   - truncate_pages is called with the correct new_page_count on success
 *   - truncate_pages is NOT called when purify fails
 *
 * source-14k1: this file used to keep private copies of dir_insert_ctx_t,
 * uid_t and status_$t and to #define DIR_INTERNAL_H so the real header stayed
 * out.  The private context typed the directory handle as uintptr_t, so it
 * silently disagreed with the real one.  It now includes dir/dir_internal.h
 * and mocks only the callees, like dir/test/test_insert_entry.c does.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#include "dir/dir_internal.h"

/* ================================================================
 * Mock globals
 * ================================================================ */

static uint8_t mock_handle[32];

/* dir_$purify_split_pages mock */
static int purify_called = 0;
static status_$t purify_status = 0;
static dir_insert_ctx_t *purify_ctx_arg = NULL;

void dir_$purify_split_pages(dir_insert_ctx_t *ctx, status_$t *status_ret) {
    purify_called = 1;
    purify_ctx_arg = ctx;
    *status_ret = purify_status;
}

/* dir_$truncate_pages mock */
static int truncate_called = 0;
static void *truncate_handle_arg = NULL;
static uint16_t truncate_page_count_arg = 0;
static status_$t truncate_status = 0;

uint32_t dir_$truncate_pages(void *handle, uint16_t new_page_count,
                             status_$t *status_ret) {
    truncate_called = 1;
    truncate_handle_arg = handle;
    truncate_page_count_arg = new_page_count;
    *status_ret = truncate_status;
    return 0;
}

/*
 * dir/finalize_split.c turns the 32-bit directory handle back into a pointer
 * with NAME_$HANDLE_TO_PTR (name/name.h).  On a 64-bit host that is the
 * registry in name/handle_map.c, so it is compiled in here and the tests
 * register mock_handle with NAME_$PTR_TO_HANDLE.
 */
#include "../../name/handle_map.c"

/* Pull in the implementation */
#include "../finalize_split.c"

/* ================================================================
 * Helper: reset all test state
 * ================================================================ */
static void reset_test_state(void) {
    memset(mock_handle, 0, sizeof(mock_handle));
    purify_called = 0;
    purify_status = 0;
    purify_ctx_arg = NULL;
    truncate_called = 0;
    truncate_handle_arg = NULL;
    truncate_page_count_arg = 0;
    truncate_status = 0;
}

/* ================================================================
 * Test cases
 * ================================================================ */

/*
 * Test 1: Basic finalize with max_depth=2, current_slot=1.
 *
 * diff = 2 - 1 = 1, so the loop runs 2 iterations (i=0,1).
 * split_pages[1+1] = split_pages[2] is saved before overwrite.
 * Then path_page[1] -> split_pages[1], path_page[2] -> split_pages[2].
 */
TEST(basic_finalize) {
    reset_test_state();

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 2;
    ctx.current_slot = 1;

    /* Pre-populate split_pages as alloc_split_page would have */
    ctx.split_pages[0] = 0;
    ctx.split_pages[1] = 10;  /* base page 1 */
    ctx.split_pages[2] = 11;  /* base page 2 - will be saved as new_page_count */
    ctx.split_pages[3] = 5;   /* extra page */

    /* path_page reflects final B-tree structure after split */
    ctx.path_page[1] = 20;
    ctx.path_page[2] = 21;

    ctx.page_count = 2;

    status_$t status = status_$ok;
    dir_$finalize_split(&ctx, &status);

    ASSERT_EQ(status_$ok, status);

    /* split_pages[1] and [2] should now have path_page values */
    ASSERT_EQ(20, ctx.split_pages[1]);
    ASSERT_EQ(21, ctx.split_pages[2]);

    /* Extra page should be untouched */
    ASSERT_EQ(5, ctx.split_pages[3]);

    /* purify should have been called */
    ASSERT_EQ(1, purify_called);
    ASSERT_EQ((uintptr_t)&ctx, (uintptr_t)purify_ctx_arg);

    /* truncate should have been called with saved value (11) */
    ASSERT_EQ(1, truncate_called);
    ASSERT_EQ((uintptr_t)mock_handle, (uintptr_t)truncate_handle_arg);
    ASSERT_EQ(11, truncate_page_count_arg);
}

/*
 * Test 2: Purify failure - truncate should NOT be called.
 */
TEST(purify_failure_skips_truncate) {
    reset_test_state();
    purify_status = 0x00030001;  /* Some error */

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 1;
    ctx.current_slot = 0;
    ctx.split_pages[1] = 5;  /* saved value */
    ctx.split_pages[2] = 6;
    ctx.path_page[0] = 10;
    ctx.path_page[1] = 11;
    ctx.page_count = 2;

    status_$t status = status_$ok;
    dir_$finalize_split(&ctx, &status);

    /* Status should reflect the purify error */
    ASSERT_EQ(0x00030001, status);

    /* Purify was called */
    ASSERT_EQ(1, purify_called);

    /* Truncate should NOT have been called */
    ASSERT_EQ(0, truncate_called);
}

/*
 * Test 3: diff < 0 (max_depth < current_slot) - loop should be skipped.
 *
 * This is an edge case where no path pages need to be copied.
 */
TEST(negative_diff_skips_loop) {
    reset_test_state();

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 0;
    ctx.current_slot = 1;

    /* split_pages should remain unchanged since loop is skipped */
    ctx.split_pages[0] = 99;
    ctx.split_pages[1] = 88;
    ctx.split_pages[2] = 77;
    ctx.page_count = 1;

    status_$t status = status_$ok;
    dir_$finalize_split(&ctx, &status);

    ASSERT_EQ(status_$ok, status);

    /* split_pages should be untouched */
    ASSERT_EQ(99, ctx.split_pages[0]);
    ASSERT_EQ(88, ctx.split_pages[1]);
    ASSERT_EQ(77, ctx.split_pages[2]);

    /* purify and truncate should still be called */
    ASSERT_EQ(1, purify_called);
    ASSERT_EQ(1, truncate_called);
}

/*
 * Test 4: diff = 0 (max_depth == current_slot) - loop runs once.
 *
 * Only one path page is copied: path_page[current_slot] -> split_pages[1].
 * The saved value is split_pages[1] before overwrite.
 */
TEST(diff_zero_single_copy) {
    reset_test_state();

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 3;
    ctx.current_slot = 3;

    ctx.split_pages[1] = 15;  /* Will be saved and then overwritten */
    ctx.split_pages[2] = 42;  /* Should remain untouched */
    ctx.path_page[3] = 30;
    ctx.page_count = 1;

    status_$t status = status_$ok;
    dir_$finalize_split(&ctx, &status);

    ASSERT_EQ(status_$ok, status);

    /* split_pages[1] should be overwritten with path_page[3] */
    ASSERT_EQ(30, ctx.split_pages[1]);

    /* split_pages[2] should be untouched */
    ASSERT_EQ(42, ctx.split_pages[2]);

    /* truncate should have been called with saved value (15) */
    ASSERT_EQ(1, truncate_called);
    ASSERT_EQ(15, truncate_page_count_arg);
}

/*
 * Test 5: Larger diff (max_depth=4, current_slot=1) - loop runs 4 times.
 */
TEST(large_diff_multi_copy) {
    reset_test_state();

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 4;
    ctx.current_slot = 1;

    /* Pre-populate split_pages */
    ctx.split_pages[1] = 50;
    ctx.split_pages[2] = 51;
    ctx.split_pages[3] = 52;
    ctx.split_pages[4] = 53;  /* diff=3, saved = split_pages[1+3] = split_pages[4] = 53 */
    ctx.split_pages[5] = 60;  /* extra, untouched */

    /* path_page values to copy */
    ctx.path_page[1] = 100;
    ctx.path_page[2] = 101;
    ctx.path_page[3] = 102;
    ctx.path_page[4] = 103;

    ctx.page_count = 4;

    status_$t status = status_$ok;
    dir_$finalize_split(&ctx, &status);

    ASSERT_EQ(status_$ok, status);

    /* split_pages[1..4] should have path_page values */
    ASSERT_EQ(100, ctx.split_pages[1]);
    ASSERT_EQ(101, ctx.split_pages[2]);
    ASSERT_EQ(102, ctx.split_pages[3]);
    ASSERT_EQ(103, ctx.split_pages[4]);

    /* split_pages[5] should be untouched */
    ASSERT_EQ(60, ctx.split_pages[5]);

    /* truncate called with saved value 53 */
    ASSERT_EQ(53, truncate_page_count_arg);
}

/*
 * Test 6: Truncate failure propagates status (but purify succeeded).
 */
TEST(truncate_failure_propagates) {
    reset_test_state();
    truncate_status = 0x00050002;  /* Some truncate error */

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(mock_handle);
    ctx.max_depth = 1;
    ctx.current_slot = 0;
    ctx.split_pages[1] = 7;
    ctx.split_pages[2] = 8;
    ctx.path_page[0] = 20;
    ctx.path_page[1] = 21;
    ctx.page_count = 2;

    status_$t status = status_$ok;
    dir_$finalize_split(&ctx, &status);

    /* Status should reflect the truncate error */
    ASSERT_EQ(0x00050002, status);

    /* Both purify and truncate should have been called */
    ASSERT_EQ(1, purify_called);
    ASSERT_EQ(1, truncate_called);
}

/*
 * Test 7: Verify handle is passed correctly to truncate.
 *
 * Uses a distinct mock handle address to verify the pointer is forwarded.
 */
TEST(handle_passed_to_truncate) {
    reset_test_state();

    static uint8_t other_handle[32];
    memset(other_handle, 0xAB, sizeof(other_handle));

    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.handle = NAME_$PTR_TO_HANDLE(other_handle);
    ctx.max_depth = 1;
    ctx.current_slot = 1;
    ctx.split_pages[1] = 3;
    ctx.path_page[1] = 10;
    ctx.page_count = 1;

    status_$t status = status_$ok;
    dir_$finalize_split(&ctx, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ((uintptr_t)other_handle, (uintptr_t)truncate_handle_arg);
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    printf("dir_$finalize_split tests:\n");

    RUN_TEST(basic_finalize);
    RUN_TEST(purify_failure_skips_truncate);
    RUN_TEST(negative_diff_skips_loop);
    RUN_TEST(diff_zero_single_copy);
    RUN_TEST(large_diff_multi_copy);
    RUN_TEST(truncate_failure_propagates);
    RUN_TEST(handle_passed_to_truncate);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
