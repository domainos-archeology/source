/*
 * file/test/test_fw_file.c - Unit tests for FILE_$FW_FILE
 *
 * FILE_$FW_FILE asks FILE_$DELETE_INT whether the file is locked and then
 * purifies it with AST_$PURIFY: local-only (0x0002) when locked, with
 * remote sync (0x8002) otherwise.  Both callees are mocked here.
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

/*
 * The shared in-code zero longword at 0x00E5E61E, normally defined in
 * file/file_data.c.  FILE_$FW_FILE hands its ADDRESS to AST_$PURIFY as the
 * segment list ("pea (-0x48,PC)" at 0x00E5E664), so the test needs the
 * storage even though nothing reads through it.
 */
#include <stdint.h>
uint32_t file_$nil_cell = 0;

#include "../fw_file.c"

/* ============================================================================
 * Mocks
 * ============================================================================ */

static int mock_delete_int_called;
static uid_t *mock_delete_int_uid;
static uint16_t mock_delete_int_flags;
static int8_t mock_delete_int_return;

static int mock_purify_called;
static uid_t *mock_purify_uid;
static uint16_t mock_purify_flags;
static int16_t mock_purify_segment;
static uint32_t *mock_purify_page_list;
static uint16_t mock_purify_page_count;
static status_$t mock_purify_status;

static void reset_mocks(void)
{
    mock_delete_int_called = 0;
    mock_delete_int_uid = NULL;
    mock_delete_int_flags = 0;
    mock_delete_int_return = 0;

    mock_purify_called = 0;
    mock_purify_uid = NULL;
    mock_purify_flags = 0;
    mock_purify_segment = 0;
    mock_purify_page_list = NULL;
    mock_purify_page_count = 0;
    mock_purify_status = status_$ok;
}

int8_t FILE_$DELETE_INT(uid_t *file_uid, uint16_t flags, uint8_t *result,
                        status_$t *status_ret)
{
    mock_delete_int_called++;
    mock_delete_int_uid = file_uid;
    mock_delete_int_flags = flags;
    *result = 0;
    *status_ret = status_$ok;
    return mock_delete_int_return;
}

uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused,
                     status_$t *status)
{
    mock_purify_called++;
    mock_purify_uid = uid;
    mock_purify_flags = flags;
    mock_purify_segment = segment;
    mock_purify_page_list = segment_list;
    mock_purify_page_count = unused;
    *status = mock_purify_status;
    return 0;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(unlocked_file_purifies_with_remote)
{
    uid_t test_uid = { 0x12345678, 0xABCDEF00 };
    status_$t status = 0xFFFFFFFF;

    reset_mocks();
    mock_delete_int_return = 0;  /* not locked */

    FILE_$FW_FILE(&test_uid, &status);

    ASSERT_EQ(1, mock_delete_int_called);
    ASSERT_EQ(&test_uid, mock_delete_int_uid);
    ASSERT_EQ(0, mock_delete_int_flags);
    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(&test_uid, mock_purify_uid);
    ASSERT_EQ(FW_PURIFY_WITH_REMOTE, mock_purify_flags);
    ASSERT_EQ(0, mock_purify_segment);
    /*
     * The segment list is the shared in-code zero longword at 0x00E5E61E
     * ("pea (-0x48,PC)" at 0x00E5E664), NOT nil.
     */
    ASSERT_EQ(&file_$nil_cell, mock_purify_page_list);
    ASSERT_EQ(0, mock_purify_page_count);
    ASSERT_EQ(status_$ok, status);
}

TEST(locked_file_purifies_locally)
{
    uid_t test_uid = { 0x12345678, 0xABCDEF00 };
    status_$t status = 0xFFFFFFFF;

    reset_mocks();
    mock_delete_int_return = -1;  /* locked */

    FILE_$FW_FILE(&test_uid, &status);

    ASSERT_EQ(1, mock_delete_int_called);
    ASSERT_EQ(1, mock_purify_called);
    ASSERT_EQ(FW_PURIFY_LOCAL_ONLY, mock_purify_flags);
    ASSERT_EQ(status_$ok, status);
}

TEST(purify_status_is_returned)
{
    uid_t test_uid = { 1, 2 };
    status_$t status = 0;

    reset_mocks();
    mock_purify_status = 0x00050003;

    FILE_$FW_FILE(&test_uid, &status);

    ASSERT_EQ(0x00050003, status);
}

TEST(purify_flag_values)
{
    ASSERT_EQ(0x0002, FW_PURIFY_LOCAL_ONLY);
    ASSERT_EQ(0x8002, FW_PURIFY_WITH_REMOTE);
}

int main(void)
{
    printf("FILE_$FW_FILE tests\n");
    RUN_TEST(unlocked_file_purifies_with_remote);
    RUN_TEST(locked_file_purifies_locally);
    RUN_TEST(purify_status_is_returned);
    RUN_TEST(purify_flag_values);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}

