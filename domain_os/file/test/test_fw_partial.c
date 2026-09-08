/*
 * file/test/test_fw_partial.c - Unit tests for FILE_$FW_PARTIAL
 *
 * FILE_$FW_PARTIAL purifies every 32KB page that overlaps a byte range,
 * stopping at the first error.  FILE_$DELETE_INT and AST_$PURIFY are mocked.
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

#include "../fw_partial.c"

/* ============================================================================
 * Mocks
 * ============================================================================ */

#define MAX_PURIFY_CALLS 16

static int mock_delete_int_called;
static int8_t mock_delete_int_return;

static int mock_purify_called;
static uint16_t mock_purify_flags[MAX_PURIFY_CALLS];
static int16_t mock_purify_segment[MAX_PURIFY_CALLS];
static status_$t mock_purify_status[MAX_PURIFY_CALLS];
static uint32_t *mock_purify_seglist[MAX_PURIFY_CALLS];

/* The shared zero longword at 0x00E5E61E, owned by file/file_data.c. */
uint32_t file_$nil_cell = 0;

static void reset_mocks(void)
{
    int i;
    mock_delete_int_called = 0;
    mock_delete_int_return = 0;
    mock_purify_called = 0;
    for (i = 0; i < MAX_PURIFY_CALLS; i++) {
        mock_purify_flags[i] = 0;
        mock_purify_segment[i] = -1;
        mock_purify_status[i] = status_$ok;
        mock_purify_seglist[i] = NULL;
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
    (void)uid; (void)unused;
    if (mock_purify_called < MAX_PURIFY_CALLS) {
        mock_purify_flags[mock_purify_called] = flags;
        mock_purify_segment[mock_purify_called] = segment;
        mock_purify_seglist[mock_purify_called] = segment_list;
        *status = mock_purify_status[mock_purify_called];
    } else {
        *status = status_$ok;
    }
    mock_purify_called++;
    return 0;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(single_page_range)
{
    uid_t uid = { 1, 2 };
    uint32_t offset = 0;
    int32_t count = 100;
    status_$t status = 0xFFFFFFFF;

    reset_mocks();
    FILE_$FW_PARTIAL(&uid, &offset, &count, &status);

    ASSERT_EQ(1, mock_delete_int_called);
    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(0, mock_purify_segment[0]);
    ASSERT_EQ(FW_PARTIAL_REMOTE, mock_purify_flags[0]);
    /*
     * 0x00E5E6EC "pea (-0xd0,PC)" = 0x00E5E61E, the shared zero longword -
     * not nil.  (source-uu9e)
     */
    ASSERT_EQ((uintptr_t)&file_$nil_cell, (uintptr_t)mock_purify_seglist[0]);
    ASSERT_EQ(status_$ok, status);
}

TEST(range_crossing_page_boundary)
{
    uid_t uid = { 1, 2 };
    uint32_t offset = 0x7F00;
    int32_t count = 0x200;
    status_$t status;

    reset_mocks();
    FILE_$FW_PARTIAL(&uid, &offset, &count, &status);

    /* 0x100 bytes remain in page 0, the rest spills into page 1 */
    ASSERT_EQ(2, mock_purify_called);
    ASSERT_EQ(0, mock_purify_segment[0]);
    ASSERT_EQ(1, mock_purify_segment[1]);
}

TEST(full_page_plus_partial)
{
    uid_t uid = { 1, 2 };
    uint32_t offset = 0x10000;   /* page 2 */
    int32_t count = 0x8001;      /* page 2 entirely plus 1 byte of page 3 */
    status_$t status;

    reset_mocks();
    FILE_$FW_PARTIAL(&uid, &offset, &count, &status);

    ASSERT_EQ(2, mock_purify_called);
    ASSERT_EQ(2, mock_purify_segment[0]);
    ASSERT_EQ(3, mock_purify_segment[1]);
}

TEST(locked_file_uses_local_flags)
{
    uid_t uid = { 1, 2 };
    uint32_t offset = 0x1000;
    int32_t count = 1;
    status_$t status;

    reset_mocks();
    mock_delete_int_return = -1;
    FILE_$FW_PARTIAL(&uid, &offset, &count, &status);

    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(FW_PARTIAL_LOCAL, mock_purify_flags[0]);
}

TEST(error_stops_iteration)
{
    uid_t uid = { 1, 2 };
    uint32_t offset = 0;
    int32_t count = 0x30000;     /* pages 0..5 */
    status_$t status;

    reset_mocks();
    mock_purify_status[1] = 0x00050001;
    FILE_$FW_PARTIAL(&uid, &offset, &count, &status);

    ASSERT_EQ(2, mock_purify_called);
    ASSERT_EQ(0x00050001, status);
}

TEST(zero_length_range_purifies_nothing)
{
    uid_t uid = { 1, 2 };
    uint32_t offset = 0x1234;
    int32_t count = 0;
    status_$t status = 0xFFFFFFFF;

    reset_mocks();
    FILE_$FW_PARTIAL(&uid, &offset, &count, &status);

    ASSERT_EQ(1, mock_delete_int_called);
    ASSERT_EQ(0, mock_purify_called);
    ASSERT_EQ(status_$ok, status);
}

TEST(constants)
{
    ASSERT_EQ(0x8000, FILE_PAGE_SIZE);
    ASSERT_EQ(0x7FFF, FILE_PAGE_MASK);
    ASSERT_EQ(0x0003, FW_PARTIAL_LOCAL);
    ASSERT_EQ(0x8003, FW_PARTIAL_REMOTE);
}

int main(void)
{
    printf("FILE_$FW_PARTIAL tests\n");
    RUN_TEST(single_page_range);
    RUN_TEST(range_crossing_page_boundary);
    RUN_TEST(full_page_plus_partial);
    RUN_TEST(locked_file_uses_local_flags);
    RUN_TEST(error_stops_iteration);
    RUN_TEST(zero_length_range_purifies_nothing);
    RUN_TEST(constants);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}

