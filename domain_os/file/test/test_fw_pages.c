/*
 * file/test/test_fw_pages.c - Unit tests for FILE_$FW_PAGES
 *
 * FILE_$FW_PAGES purifies an explicit page list in sorted batches of
 * FW_BATCH_SIZE pages.  FILE_$DELETE_INT and AST_$PURIFY are mocked.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

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
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * route/route.h (pulled in by file/file_internal.h) asserts that
 * sizeof(route_$port_t) == 0x5C, which only holds with 32-bit pointers.
 * Nothing under test here touches ROUTE data, so skip that header on the
 * host (the same approach test_uid_lock.c takes for headers it stubs).
 */
#define ROUTE_H

#include "../fw_pages.c"

/* ============================================================================
 * Mocks
 * ============================================================================ */

#define MAX_PURIFY_CALLS 8

static int mock_delete_int_called;
static int8_t mock_delete_int_return;

static int mock_purify_called;
static uint16_t mock_purify_flags[MAX_PURIFY_CALLS];
static uint16_t mock_purify_count[MAX_PURIFY_CALLS];
static uint32_t mock_purify_batch[MAX_PURIFY_CALLS][FW_BATCH_SIZE];
static status_$t mock_purify_status[MAX_PURIFY_CALLS];

static void reset_mocks(void)
{
    int i;
    mock_delete_int_called = 0;
    mock_delete_int_return = 0;
    mock_purify_called = 0;
    for (i = 0; i < MAX_PURIFY_CALLS; i++) {
        mock_purify_flags[i] = 0;
        mock_purify_count[i] = 0;
        mock_purify_status[i] = status_$ok;
        memset(mock_purify_batch[i], 0, sizeof(mock_purify_batch[i]));
    }
}

int8_t FILE_$DELETE_INT(uid_t *file_uid, uint16_t flags, uint8_t *result,
                        status_$t *status_ret)
{
    (void)file_uid; (void)flags;
    mock_delete_int_called++;
    *result = 0;
    *status_ret = status_$ok;
    return mock_delete_int_return;
}

uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused,
                     status_$t *status)
{
    (void)uid; (void)segment;
    if (mock_purify_called < MAX_PURIFY_CALLS) {
        int n = mock_purify_called;
        mock_purify_flags[n] = flags;
        mock_purify_count[n] = unused;
        memcpy(mock_purify_batch[n], segment_list,
               (unused <= FW_BATCH_SIZE ? unused : FW_BATCH_SIZE) * sizeof(uint32_t));
        *status = mock_purify_status[n];
    } else {
        *status = status_$ok;
    }
    mock_purify_called++;
    return 0;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(empty_list_returns_immediately)
{
    uid_t uid = { 1, 2 };
    uint32_t pages[1] = { 5 };
    uint16_t count = 0;
    status_$t status = 0x1234;

    reset_mocks();
    FILE_$FW_PAGES(&uid, pages, &count, &status);

    ASSERT_EQ(0, mock_delete_int_called);
    ASSERT_EQ(0, mock_purify_called);
    ASSERT_EQ(status_$ok, status);   /* status is initialised before the empty check */
}

/*
 * 0x00E5E80C-0x00E5E82A: the exchange runs when batch[j] > batch[i], so the
 * batch AST_$PURIFY receives is DESCENDING unsigned.  (source-87da)
 */
TEST(single_batch_is_sorted_descending)
{
    uid_t uid = { 1, 2 };
    uint32_t pages[5] = { 50, 10, 30, 20, 40 };
    uint16_t count = 5;
    status_$t status;
    int i;

    reset_mocks();
    FILE_$FW_PAGES(&uid, pages, &count, &status);

    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(5, mock_purify_count[0]);
    ASSERT_EQ(FW_PAGES_REMOTE, mock_purify_flags[0]);
    for (i = 0; i < 5; i++) {
        ASSERT_EQ((5 - i) * 10, mock_purify_batch[0][i]);
    }
    /* The caller's list is not modified (sorting happens on a copy) */
    ASSERT_EQ(50, pages[0]);
    ASSERT_EQ(status_$ok, status);
}

/* The comparison is `cmp.l`/`bls`, i.e. UNSIGNED: 0x80000000 outranks 1. */
TEST(sort_is_unsigned)
{
    uid_t uid = { 1, 2 };
    uint32_t pages[4] = { 1u, 0x80000000u, 2u, 0xFFFFFFFFu };
    uint16_t count = 4;
    status_$t status;

    reset_mocks();
    FILE_$FW_PAGES(&uid, pages, &count, &status);

    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(0xFFFFFFFFu, mock_purify_batch[0][0]);
    ASSERT_EQ(0x80000000u, mock_purify_batch[0][1]);
    ASSERT_EQ(2u, mock_purify_batch[0][2]);
    ASSERT_EQ(1u, mock_purify_batch[0][3]);
}

/* A batch of one takes the 0x00E5E7C4 `beq` and skips the sort entirely. */
TEST(single_page_batch_skips_sort)
{
    uid_t uid = { 1, 2 };
    uint32_t pages[1] = { 0x1234 };
    uint16_t count = 1;
    status_$t status;

    reset_mocks();
    FILE_$FW_PAGES(&uid, pages, &count, &status);

    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(1, mock_purify_count[0]);
    ASSERT_EQ(0x1234, mock_purify_batch[0][0]);
}

TEST(fifty_pages_split_into_32_and_18)
{
    uid_t uid = { 1, 2 };
    uint32_t pages[50];
    uint16_t count = 50;
    status_$t status;
    int i;

    for (i = 0; i < 50; i++) {
        pages[i] = (uint32_t)(49 - i);   /* descending */
    }

    reset_mocks();
    FILE_$FW_PAGES(&uid, pages, &count, &status);

    ASSERT_EQ(2, mock_purify_called);
    ASSERT_EQ(32, mock_purify_count[0]);
    ASSERT_EQ(18, mock_purify_count[1]);
    /* first batch held entries 1..32 of the list = pages 49..18, descending */
    ASSERT_EQ(49, mock_purify_batch[0][0]);
    ASSERT_EQ(18, mock_purify_batch[0][31]);
    /* second batch held entries 33..50 = pages 17..0, descending */
    ASSERT_EQ(17, mock_purify_batch[1][0]);
    ASSERT_EQ(0, mock_purify_batch[1][17]);
}

TEST(exactly_32_pages_is_one_batch)
{
    uid_t uid = { 1, 2 };
    uint32_t pages[32];
    uint16_t count = 32;
    status_$t status;
    int i;

    for (i = 0; i < 32; i++) {
        pages[i] = (uint32_t)i;
    }

    reset_mocks();
    FILE_$FW_PAGES(&uid, pages, &count, &status);

    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(32, mock_purify_count[0]);
}

TEST(locked_file_uses_local_flags)
{
    uid_t uid = { 1, 2 };
    uint32_t pages[2] = { 3, 4 };
    uint16_t count = 2;
    status_$t status;

    reset_mocks();
    mock_delete_int_return = -1;
    FILE_$FW_PAGES(&uid, pages, &count, &status);

    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(FW_PAGES_LOCAL, mock_purify_flags[0]);
}

TEST(error_stops_batching)
{
    uid_t uid = { 1, 2 };
    uint32_t pages[70];
    uint16_t count = 70;
    status_$t status;
    int i;

    for (i = 0; i < 70; i++) {
        pages[i] = (uint32_t)i;
    }

    reset_mocks();
    mock_purify_status[0] = 0x00050001;
    FILE_$FW_PAGES(&uid, pages, &count, &status);

    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(0x00050001, status);
}

TEST(constants)
{
    ASSERT_EQ(32, FW_BATCH_SIZE);
    ASSERT_EQ(0x0012, FW_PAGES_LOCAL);
    ASSERT_EQ(0x8012, FW_PAGES_REMOTE);
}

int main(void)
{
    printf("FILE_$FW_PAGES tests\n");
    RUN_TEST(empty_list_returns_immediately);
    RUN_TEST(single_batch_is_sorted_descending);
    RUN_TEST(sort_is_unsigned);
    RUN_TEST(single_page_batch_skips_sort);
    RUN_TEST(fifty_pages_split_into_32_and_18);
    RUN_TEST(exactly_32_pages_is_one_batch);
    RUN_TEST(locked_file_uses_local_flags);
    RUN_TEST(error_stops_batching);
    RUN_TEST(constants);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}

