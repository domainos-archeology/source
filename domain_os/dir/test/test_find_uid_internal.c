/*
 * dir/test/test_find_uid_internal.c - Unit tests for dir_$find_uid_internal
 *
 * Tests the find_uid_internal function which is the shared implementation
 * for DIR_$FIND_UID and DIR_$FIND_NET.
 *
 * Strategy: Mock DIR_$DO_OP, DIR_$OLD_FIND_UID, and DIR_$OLD_FIND_NET
 * to exercise the three main code paths:
 *   1. New protocol success (DO_OP returns ok)
 *   2. Old protocol fallback (DO_OP returns bad_reply or bad_directory)
 *   3. Error passthrough (DO_OP returns other error)
 */

/* Suppress POSIX uid_t so base/base.h can define Apollo's uid_t struct */
#define uid_t posix_uid_t
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#undef uid_t

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

#define ASSERT_STR_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("FAILED\n    String mismatch at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ===== Type Definitions ===== */

#include "dir/dir_internal.h"

/* Status codes used by find_uid_internal */
#define file_$bad_reply_received_from_remote_node  0x000F0003

/* ===== Mock State ===== */

/* Track what DIR_$DO_OP was called with */
static int do_op_called = 0;
static int16_t do_op_req_size = 0;
static int16_t do_op_resp_size = 0;

/* Control what DIR_$DO_OP returns */
static status_$t do_op_return_status = 0;
static uint16_t do_op_return_name_length = 0;
static uint32_t do_op_return_net_value = 0;
static char do_op_return_name_data[256];

/* Track OLD_FIND_UID calls */
static int old_find_uid_called = 0;
static uid_t old_find_uid_dir;
static uid_t old_find_uid_target;
static char old_find_uid_return_name[32];
static int16_t old_find_uid_return_len = 0;
static status_$t old_find_uid_return_status = 0;

/* Track OLD_FIND_NET calls */
static int old_find_net_called = 0;
static uid_t old_find_net_dir;
static uint32_t old_find_net_index = 0;
static uint32_t old_find_net_return_value = 0;

/* ===== The DIR module block ===== */

/*
 * DIR_$DATA; DIR_$OP_TAB is its op_tab at A5+0x2042 (0x00E7FC42), so the
 * FIND_UID record (opcode 0x46 >> 1 = 0x23, minus the table's 21-record
 * bias: record 14) is A5+0x20B2 / A5+0x20B6 - the two cells reset_mocks()
 * below fills in.
 */
MODULE_DATA_DEFINE(dir_$data_t, DIR_$DATA, 0x00E7DBF8);

/* ===== Mock Functions ===== */

/*
 * Mock DIR_$DO_OP - simulates the DO_OP response buffer.
 *
 * The response layout for find_uid (verified from assembly):
 *   offset 0x04: status (status_$t)
 *   offset 0x14: name_length (uint16_t)
 *   offset 0x16: net_value (uint32_t)
 *   offset 0x1A: name_data (uint8_t[])
 *
 * We fill the response buffer at the correct offsets so that
 * the packed struct in find_uid_internal reads the right values.
 */
void DIR_$DO_OP(void *request, int16_t req_size, int16_t resp_size,
                void *response, uint16_t *resp_buf)
{
    (void)request;
    (void)resp_buf;

    /* Record call parameters */
    do_op_called = 1;
    do_op_req_size = req_size;
    do_op_resp_size = resp_size;

    /* Fill the response buffer using the packed struct layout:
     * status at offset 4, name_length at 0x14, net_value at 0x16,
     * name_data at 0x1A */
    uint8_t *resp = (uint8_t *)response;
    memset(resp, 0, resp_size);

    /* Status at offset 4 */
    uint32_t status = do_op_return_status;
    memcpy(resp + 4, &status, 4);

    /* Name length at offset 0x14 */
    uint16_t name_len = do_op_return_name_length;
    memcpy(resp + 0x14, &name_len, 2);

    /* Net value at offset 0x16 */
    uint32_t net_val = do_op_return_net_value;
    memcpy(resp + 0x16, &net_val, 4);

    /* Name data at offset 0x1A */
    if (do_op_return_name_length > 0) {
        memcpy(resp + 0x1A, do_op_return_name_data, do_op_return_name_length);
    }
}

void DIR_$OLD_FIND_UID(uid_t *dir_uid, uid_t *target_uid, char *name_buf,
                       int16_t *name_len_ret, status_$t *status_ret)
{
    old_find_uid_called = 1;
    old_find_uid_dir = *dir_uid;
    old_find_uid_target = *target_uid;

    if (old_find_uid_return_len > 0) {
        memcpy(name_buf, old_find_uid_return_name, old_find_uid_return_len);
    }
    *name_len_ret = old_find_uid_return_len;
    *status_ret = old_find_uid_return_status;
}

uint32_t DIR_$OLD_FIND_NET(uid_t *dir_uid, uint32_t *index)
{
    old_find_net_called = 1;
    old_find_net_dir = *dir_uid;
    old_find_net_index = *index;
    return old_find_net_return_value;
}

/* ===== Reset mock state ===== */
static void reset_mocks(void)
{
    do_op_called = 0;
    do_op_req_size = 0;
    do_op_resp_size = 0;
    do_op_return_status = 0;
    do_op_return_name_length = 0;
    do_op_return_net_value = 0;
    memset(do_op_return_name_data, 0, sizeof(do_op_return_name_data));

    old_find_uid_called = 0;
    memset(&old_find_uid_dir, 0, sizeof(old_find_uid_dir));
    memset(&old_find_uid_target, 0, sizeof(old_find_uid_target));
    memset(old_find_uid_return_name, 0, sizeof(old_find_uid_return_name));
    old_find_uid_return_len = 0;
    old_find_uid_return_status = 0;

    old_find_net_called = 0;
    memset(&old_find_net_dir, 0, sizeof(old_find_net_dir));
    old_find_net_index = 0;
    old_find_net_return_value = 0;

    /* The FIND_UID record: version at A5+0x20b2 and req_size at A5+0x20b6 */
    memset(&DIR_$DATA, 0, sizeof(DIR_$DATA));
    DIR_$OP_REC(DIR_OP_FIND_UID >> 1).version = 0x0001;
    DIR_$OP_REC(DIR_OP_FIND_UID >> 1).base_size = 0x000a;   /* req_size = 10 */
}

/* Pull in the implementation directly */
#include "../find_uid_internal.c"

/* ===== Tests ===== */

/* Test: UID search, new protocol succeeds with name */
TEST(uid_search_success)
{
    reset_mocks();

    uid_t dir_uid = { 0x11111111, 0x22222222 };
    uid_t target_uid = { 0x33333333, 0x44444444 };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* Set up DO_OP to return success with name "hello" */
    do_op_return_status = status_$ok;
    do_op_return_name_length = 5;
    memcpy(do_op_return_name_data, "hello", 5);

    memset(name_buf, 0, sizeof(name_buf));

    dir_$find_uid_internal(&dir_uid, &target_uid, 0, 32, name_buf,
                           &name_len, &net_ret, &status);

    ASSERT_EQ(1, do_op_called);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(5, name_len);
    ASSERT_STR_EQ("hello", name_buf, 5);
    ASSERT_EQ(0, old_find_uid_called);
    ASSERT_EQ(0, old_find_net_called);
}

/* Test: UID search, name truncation */
TEST(uid_search_truncated)
{
    reset_mocks();

    uid_t dir_uid = { 0x11111111, 0x22222222 };
    uid_t target_uid = { 0x33333333, 0x44444444 };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* Set up DO_OP to return success with a 10-byte name */
    do_op_return_status = status_$ok;
    do_op_return_name_length = 10;
    memcpy(do_op_return_name_data, "longername", 10);

    memset(name_buf, 0, sizeof(name_buf));

    /* Pass name_buf_len = 5 (less than returned 10) */
    dir_$find_uid_internal(&dir_uid, &target_uid, 0, 5, name_buf,
                           &name_len, &net_ret, &status);

    ASSERT_EQ(status_$naming_leaf_truncated, status);
    ASSERT_EQ(5, name_len);
    ASSERT_STR_EQ("longe", name_buf, 5);
}

/* Test: Network search, new protocol succeeds */
TEST(net_search_success)
{
    reset_mocks();

    uid_t dir_uid = { 0xAAAAAAAA, 0xBBBBBBBB };
    uid_t target_uid = { 0xCCCCCCCC, 0xDDDDDDDD };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* Set up DO_OP to return success with net value */
    do_op_return_status = status_$ok;
    do_op_return_net_value = 0x12345678;

    /* flag = 0xFF (negative when cast to int8_t) => network search */
    dir_$find_uid_internal(&dir_uid, &target_uid, (int8_t)0xFF, 0, name_buf,
                           &name_len, &net_ret, &status);

    ASSERT_EQ(1, do_op_called);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x12345678, net_ret);
    ASSERT_EQ(0, old_find_uid_called);
    ASSERT_EQ(0, old_find_net_called);
}

/* Test: UID search fallback to old protocol (bad_reply) */
TEST(uid_search_fallback_bad_reply)
{
    reset_mocks();

    uid_t dir_uid = { 0x11111111, 0x22222222 };
    uid_t target_uid = { 0x33333333, 0x44444444 };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* DO_OP returns bad_reply */
    do_op_return_status = file_$bad_reply_received_from_remote_node;

    /* OLD_FIND_UID will return this */
    old_find_uid_return_len = 4;
    memcpy(old_find_uid_return_name, "test", 4);
    old_find_uid_return_status = status_$ok;

    memset(name_buf, 0, sizeof(name_buf));

    dir_$find_uid_internal(&dir_uid, &target_uid, 0, 32, name_buf,
                           &name_len, &net_ret, &status);

    ASSERT_EQ(1, do_op_called);
    ASSERT_EQ(1, old_find_uid_called);
    ASSERT_EQ(0, old_find_net_called);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(4, name_len);
    ASSERT_STR_EQ("test", name_buf, 4);

    /* Verify the correct UIDs were passed to OLD_FIND_UID */
    ASSERT_EQ(0x11111111, old_find_uid_dir.high);
    ASSERT_EQ(0x22222222, old_find_uid_dir.low);
    ASSERT_EQ(0x33333333, old_find_uid_target.high);
    ASSERT_EQ(0x44444444, old_find_uid_target.low);
}

/* Test: UID search fallback to old protocol (bad_directory) */
TEST(uid_search_fallback_bad_directory)
{
    reset_mocks();

    uid_t dir_uid = { 0x11111111, 0x22222222 };
    uid_t target_uid = { 0x33333333, 0x44444444 };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* DO_OP returns bad_directory */
    do_op_return_status = status_$naming_bad_directory;

    old_find_uid_return_len = 3;
    memcpy(old_find_uid_return_name, "abc", 3);
    old_find_uid_return_status = status_$ok;

    memset(name_buf, 0, sizeof(name_buf));

    dir_$find_uid_internal(&dir_uid, &target_uid, 0, 32, name_buf,
                           &name_len, &net_ret, &status);

    ASSERT_EQ(1, old_find_uid_called);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(3, name_len);
}

/* Test: Network search fallback to old protocol */
TEST(net_search_fallback)
{
    reset_mocks();

    uid_t dir_uid = { 0xAAAAAAAA, 0xBBBBBBBB };
    uid_t target_uid = { 0xCCCCCCCC, 0xDD0ABCDE };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* DO_OP returns bad_reply */
    do_op_return_status = file_$bad_reply_received_from_remote_node;

    /* OLD_FIND_NET returns this value */
    old_find_net_return_value = 0x99887766;

    /* flag = 0xFF (negative) => network search */
    dir_$find_uid_internal(&dir_uid, &target_uid, (int8_t)0xFF, 0, name_buf,
                           &name_len, &net_ret, &status);

    ASSERT_EQ(1, do_op_called);
    ASSERT_EQ(0, old_find_uid_called);
    ASSERT_EQ(1, old_find_net_called);

    /* Verify masked UID was passed: low 20 bits of target_uid.low */
    ASSERT_EQ(0x0ABCDE, old_find_net_index);

    /* Verify result was stored */
    ASSERT_EQ(0x99887766, net_ret);

    /* Verify dir_uid was passed correctly */
    ASSERT_EQ(0xAAAAAAAA, old_find_net_dir.high);
    ASSERT_EQ(0xBBBBBBBB, old_find_net_dir.low);
}

/* Test: Error status passthrough (non-fallback error) */
TEST(error_passthrough)
{
    reset_mocks();

    uid_t dir_uid = { 0x11111111, 0x22222222 };
    uid_t target_uid = { 0x33333333, 0x44444444 };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* DO_OP returns an unrelated error */
    do_op_return_status = status_$naming_name_not_found;

    dir_$find_uid_internal(&dir_uid, &target_uid, 0, 32, name_buf,
                           &name_len, &net_ret, &status);

    ASSERT_EQ(1, do_op_called);
    ASSERT_EQ(0, old_find_uid_called);
    ASSERT_EQ(0, old_find_net_called);
    ASSERT_EQ(status_$naming_name_not_found, status);
}

/* Test: UID search with zero-length name */
TEST(uid_search_empty_name)
{
    reset_mocks();

    uid_t dir_uid = { 0x11111111, 0x22222222 };
    uid_t target_uid = { 0x33333333, 0x44444444 };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* Set up DO_OP to return success with empty name */
    do_op_return_status = status_$ok;
    do_op_return_name_length = 0;

    memset(name_buf, 'X', sizeof(name_buf));

    dir_$find_uid_internal(&dir_uid, &target_uid, 0, 32, name_buf,
                           &name_len, &net_ret, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, name_len);
    /* Name buffer should not be modified */
    ASSERT_EQ('X', name_buf[0]);
}

/* Test: UID search with exact buffer size */
TEST(uid_search_exact_fit)
{
    reset_mocks();

    uid_t dir_uid = { 0x11111111, 0x22222222 };
    uid_t target_uid = { 0x33333333, 0x44444444 };
    char name_buf[32];
    int16_t name_len = 0;
    uint32_t net_ret = 0;
    status_$t status = 0xDEAD;

    /* Set up DO_OP to return success with name exactly fitting buffer */
    do_op_return_status = status_$ok;
    do_op_return_name_length = 5;
    memcpy(do_op_return_name_data, "exact", 5);

    memset(name_buf, 0, sizeof(name_buf));

    /* Buffer size exactly matches name length */
    dir_$find_uid_internal(&dir_uid, &target_uid, 0, 5, name_buf,
                           &name_len, &net_ret, &status);

    /* Should succeed without truncation */
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(5, name_len);
    ASSERT_STR_EQ("exact", name_buf, 5);
}

/* ===== Main ===== */

int main(void)
{
    printf("Testing dir_$find_uid_internal...\n");

    RUN_TEST(uid_search_success);
    RUN_TEST(uid_search_truncated);
    RUN_TEST(net_search_success);
    RUN_TEST(uid_search_fallback_bad_reply);
    RUN_TEST(uid_search_fallback_bad_directory);
    RUN_TEST(net_search_fallback);
    RUN_TEST(error_passthrough);
    RUN_TEST(uid_search_empty_name);
    RUN_TEST(uid_search_exact_fit);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
