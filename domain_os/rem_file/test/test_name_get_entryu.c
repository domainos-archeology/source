/*
 * rem_file/test/test_name_get_entryu.c - unit tests for
 * REM_FILE_$NAME_GET_ENTRYU (0x00E6209A)
 *
 * The real rem_file/name_get_entryu.c is #included below and every routine
 * it calls out to is mocked here.  What is checked is the frame model the
 * 2026-09-07 pass corrected (source-fqv4): the request record is filled IN
 * PLACE, so ACL_$GET_RE_SIDS and ACL_$GET_PROJ_LIST are handed pointers
 * that land at request+0x34, request+0x58 and request+0x98, and the bytes
 * those mocks write are the bytes REM_FILE_$SEND_REQUEST sees.
 *
 * Also covered:
 *   - the fixed request bytes 0x80 / 0x1C / flags 3 (0x00E620BC..0x00E620EA)
 *   - the unconditional 32-byte name copy at 0x00E620DC, which is NOT
 *     clipped to the caller's name_len
 *   - the lengths 0xA2 and 0xBE handed to REM_FILE_$SEND_REQUEST
 *     (0x00E62170, 0x00E62162)
 *   - the two early returns on a bad status (0x00E62122, 0x00E62140)
 *   - the reply unpack at 0x00E6218C..0x00E621A8, including the
 *     received_len == 0x22 arm that zeroes the extra longword
 */

#include <stdio.h>
#include <string.h>

#include "rem_file/rem_file_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-52s ", #name);                                            \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed == 0) { printf("PASSED\n"); }                      \
    } while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
        unsigned long _e = (unsigned long)(expected);                         \
        unsigned long _a = (unsigned long)(actual);                           \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected 0x%lx, got 0x%lx\n",              \
                   __LINE__, _e, _a);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * The code under test
 *
 * Included before the mocks so that the request/reply record types are in
 * scope: every assertion below reads named fields rather than raw bytes,
 * because the host is little-endian and the m68k is not.
 * ============================================================================ */

#include "../name_get_entryu.c"

/* ============================================================================
 * Globals and mocks
 * ============================================================================ */

uint16_t REM_FILE_$MAX_PROJ_LIST = 8;       /* 0xE61718 */
int16_t  ACL_$SUPER_COUNT[8];
uint16_t PROC1_$CURRENT;

/* what the last ACL_$GET_RE_SIDS call was handed */
static void *mock_re_sids_out1;
static void *mock_re_sids_out2;
static status_$t mock_re_sids_status;

/* what the last ACL_$GET_PROJ_LIST call was handed */
static void *mock_proj_list;
static int16_t *mock_proj_max;
static void *mock_proj_count;
static status_$t mock_proj_status;

/* a snapshot of the request as REM_FILE_$SEND_REQUEST saw it */
static rem_file_$name_get_entryu_req_t  mock_request;
static int16_t   mock_request_len;
static uint16_t  mock_response_max;
static void     *mock_extra_data;
static void     *mock_bulk_data;
static void     *mock_bulk_len;
static rem_file_$name_get_entryu_resp_t *mock_response_buf;
static uint16_t  mock_received_len;
static status_$t mock_send_status;
static int       mock_send_calls;

void ACL_$GET_RE_SIDS(void *original_sids, void *current_sids,
                      status_$t *status_ret)
{
    mock_re_sids_out1 = original_sids;
    mock_re_sids_out2 = current_sids;
    if (mock_re_sids_status == status_$ok) {
        memset(original_sids, 0xA1, 0x24);
        memset(current_sids,  0x5A, 0x24);
    }
    *status_ret = mock_re_sids_status;
}

void ACL_$GET_PROJ_LIST(uid_t *proj_acls, int16_t *max_count,
                        int16_t *count_ret, status_$t *status_ret)
{
    int i;

    mock_proj_list  = proj_acls;
    mock_proj_max   = max_count;
    mock_proj_count = count_ret;
    if (mock_proj_status == status_$ok) {
        for (i = 0; i < 8; i++) {
            proj_acls[i].high = 0xC0DE0000u + (uint32_t)i;
            proj_acls[i].low  = 0x0BAD0000u + (uint32_t)i;
        }
        *count_ret = 5;
    }
    *status_ret = mock_proj_status;
}

void REM_FILE_$SEND_REQUEST(void *addr_info, void *request, int16_t request_len,
                            void *extra_data, int16_t extra_len,
                            void *response, uint16_t response_max,
                            uint16_t *received_len, void *bulk_data,
                            int16_t bulk_max, int16_t *bulk_len,
                            uint16_t *packet_id, status_$t *status_ret)
{
    (void)addr_info; (void)extra_len; (void)bulk_max;

    mock_send_calls++;
    memcpy(&mock_request, request, sizeof(mock_request));
    mock_request_len  = request_len;
    mock_response_max = response_max;
    mock_extra_data   = extra_data;
    mock_bulk_data    = bulk_data;
    mock_bulk_len     = bulk_len;

    if (mock_response_buf != NULL) {
        memcpy(response, mock_response_buf, response_max);
    }
    *received_len = mock_received_len;
    *packet_id    = 0x1234;
    *status_ret   = mock_send_status;
}

/* ============================================================================
 * Fixtures
 * ============================================================================ */

static uid_t     dir_uid;
static char      name_buf[0x20];
static rem_file_$name_get_entryu_resp_t reply;
static rem_file_$name_get_entryu_result_t result;
static status_$t st;

static void reset(void)
{
    int i;

    memset(&mock_request, 0, sizeof(mock_request));
    mock_re_sids_status = status_$ok;
    mock_proj_status    = status_$ok;
    mock_send_status    = status_$ok;
    mock_received_len   = 0x30;
    mock_send_calls     = 0;
    mock_response_buf   = &reply;

    memset(&reply, 0, sizeof(reply));
    memset(&result, 0xEE, sizeof(result));
    st = 0x7FFFFFFF;

    PROC1_$CURRENT = 3;
    for (i = 0; i < 8; i++) {
        ACL_$SUPER_COUNT[i] = 0;
    }

    dir_uid.high = 0xDEADBEEFu;
    dir_uid.low  = 0xFEEDFACEu;
    for (i = 0; i < 0x20; i++) {
        name_buf[i] = (char)('a' + (i % 26));
    }
}

static void call(uint16_t name_len)
{
    REM_FILE_$NAME_GET_ENTRYU((void *)0x1000, &dir_uid, name_buf, name_len,
                              &result, &st);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* The layout source-fqv4 is about. */
TEST(request_field_offsets)
{
    ASSERT_EQ(0x04, offsetof(rem_file_$name_get_entryu_req_t, dir_uid));
    ASSERT_EQ(0x0C, offsetof(rem_file_$name_get_entryu_req_t, name));
    ASSERT_EQ(0x2C, offsetof(rem_file_$name_get_entryu_req_t, name_len));
    ASSERT_EQ(0x2E, offsetof(rem_file_$name_get_entryu_req_t, flags));
    ASSERT_EQ(0x30, offsetof(rem_file_$name_get_entryu_req_t, privileged));
    ASSERT_EQ(0x34, offsetof(rem_file_$name_get_entryu_req_t, re_sids));
    ASSERT_EQ(0x58, offsetof(rem_file_$name_get_entryu_req_t, proj_list));
    ASSERT_EQ(0x98, offsetof(rem_file_$name_get_entryu_req_t, proj_count));
    ASSERT_EQ(0x9A, offsetof(rem_file_$name_get_entryu_req_t, _zero_9a));
    ASSERT_EQ(0x9E, offsetof(rem_file_$name_get_entryu_req_t, _zero_9e));
    ASSERT_EQ(0x02, offsetof(rem_file_$name_get_entryu_result_t, entry_uid));
    ASSERT_EQ(0x0A, offsetof(rem_file_$name_get_entryu_result_t, extra_info));
}

/*
 * 0x00E62112 / 0x00E62130 / 0x00E62128: the ACL outputs go straight into
 * the request, they are not staged in locals and copied afterwards.
 */
TEST(acl_calls_write_into_the_request_in_place)
{
    reset();
    call(5);

    /* the second GET_RE_SIDS output is request+0x34 */
    ASSERT_EQ(0x5A, mock_request.re_sids[0]);
    ASSERT_EQ(0x5A, mock_request.re_sids[0x23]);
    /* the first output is a local, so it must NOT be inside the request */
    ASSERT_EQ(1, mock_re_sids_out1 != mock_re_sids_out2);

    /* GET_PROJ_LIST writes the list at +0x58 and the count at +0x98 */
    ASSERT_EQ(0xC0DE0000u, mock_request.proj_list[0].high);
    ASSERT_EQ(0x0BAD0007u, mock_request.proj_list[7].low);
    ASSERT_EQ(5,           mock_request.proj_count);

    /* and it was given &REM_FILE_$MAX_PROJ_LIST, not a UID */
    ASSERT_EQ((uintptr_t)&REM_FILE_$MAX_PROJ_LIST, (uintptr_t)mock_proj_max);

    /* the last two longwords are zeroed after the ACL calls */
    ASSERT_EQ(0, mock_request._zero_9a[0] | mock_request._zero_9a[1]);
    ASSERT_EQ(0, mock_request._zero_9e[0] | mock_request._zero_9e[1]);
    ASSERT_EQ(0, mock_request._zero_32);
}

/* The pointers themselves land where the disassembly says. */
TEST(acl_output_pointers_are_request_offsets)
{
    uintptr_t req_base;

    reset();
    call(5);

    /* the pointers ACL_$GET_RE_SIDS and ACL_$GET_PROJ_LIST were handed all
     * point into one record, at +0x34, +0x58 and +0x98 */
    req_base = (uintptr_t)mock_re_sids_out2 - 0x34;
    ASSERT_EQ(req_base + 0x58, (uintptr_t)mock_proj_list);
    ASSERT_EQ(req_base + 0x98, (uintptr_t)mock_proj_count);
}

/* 0x00E620BC, 0x00E620C2, 0x00E620EA, 0x00E620E6 */
TEST(fixed_request_bytes)
{
    reset();
    call(5);

    ASSERT_EQ(REM_FILE_REQ_MAGIC,          mock_request.magic);
    ASSERT_EQ(REM_FILE_OP_NAME_GET_ENTRYU, mock_request.opcode);
    ASSERT_EQ(5,    mock_request.name_len);
    ASSERT_EQ(3,    mock_request.flags);
    ASSERT_EQ(0x00, (uint8_t)mock_request.privileged);  /* not privileged */
    ASSERT_EQ(0xDEADBEEFu, mock_request.dir_uid.high);
    ASSERT_EQ(0xFEEDFACEu, mock_request.dir_uid.low);
    ASSERT_EQ(0xA2, mock_request_len);        /* 0x00E62170 */
    ASSERT_EQ(0xBE, mock_response_max);       /* 0x00E62162 */
    ASSERT_EQ(1,    mock_send_calls);
}

/* 0x00E620F0: sgt on ACL_$SUPER_COUNT[PROC1_$CURRENT] gives a Pascal true. */
TEST(privileged_flag_is_all_ones_when_super_count_is_positive)
{
    reset();
    ACL_$SUPER_COUNT[3] = 1;
    call(5);
    ASSERT_EQ(0xFF, (uint8_t)mock_request.privileged);
}

/*
 * 0x00E620DC: "moveq #0x1f,D1" - all 32 bytes are copied whatever name_len
 * says, so a short name_len still ships the whole buffer.
 */
TEST(name_copy_is_32_bytes_and_ignores_name_len)
{
    int i;

    reset();
    call(1);

    for (i = 0; i < 0x20; i++) {
        ASSERT_EQ((uint8_t)name_buf[i], (uint8_t)mock_request.name[i]);
    }
    ASSERT_EQ(1, mock_request.name_len);
}

/* 0x00E6217C: one zero word serves as extra_data, bulk_data and bulk_len. */
TEST(one_zero_word_is_passed_three_times)
{
    reset();
    call(5);

    ASSERT_EQ((uintptr_t)mock_extra_data, (uintptr_t)mock_bulk_data);
    ASSERT_EQ((uintptr_t)mock_extra_data, (uintptr_t)mock_bulk_len);
}

/* 0x00E62122 */
TEST(get_re_sids_failure_returns_before_sending)
{
    reset();
    mock_re_sids_status = 0x00110006;
    call(5);

    ASSERT_EQ(0x00110006, st);
    ASSERT_EQ(0, mock_send_calls);
}

/* 0x00E62140 */
TEST(get_proj_list_failure_returns_before_sending)
{
    reset();
    mock_proj_status = 0x00110007;
    call(5);

    ASSERT_EQ(0x00110007, st);
    ASSERT_EQ(0, mock_send_calls);
}

/* 0x00E6218A */
TEST(send_failure_leaves_the_result_alone)
{
    reset();
    mock_send_status = 0x000F0004;
    call(5);

    ASSERT_EQ(0x000F0004, st);
    ASSERT_EQ(0xEEEE, result.entry_type);
}

/* 0x00E6218C..0x00E621A8 */
TEST(reply_unpack)
{
    reset();
    reply.entry_type     = 0x0007;      /* +0x08 */
    reply.entry_uid.high = 0x11223344u; /* +0x2C */
    reply.entry_uid.low  = 0x55667788u;
    reply.extra_info     = 0x9ABCDEF0u; /* +0x34 */
    mock_received_len = 0x30;
    call(5);

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0x0007, result.entry_type);
    ASSERT_EQ(0x11223344u, result.entry_uid.high);
    ASSERT_EQ(0x55667788u, result.entry_uid.low);
    ASSERT_EQ(0x9ABCDEF0u, result.extra_info);
}

/* 0x00E6219C: a 0x22-byte reply carries no extra longword. */
TEST(short_reply_zeroes_the_extra_longword)
{
    reset();
    reply.entry_type = 0x0007;
    reply.extra_info = 0x9ABCDEF0u;
    mock_received_len = REM_FILE_NAME_GET_ENTRYU_SHORT_REPLY;
    call(5);

    ASSERT_EQ(0x0007, result.entry_type);
    ASSERT_EQ(0, result.extra_info);
}

int main(void)
{
    printf("test_name_get_entryu:\n");

    RUN_TEST(request_field_offsets);
    RUN_TEST(acl_calls_write_into_the_request_in_place);
    RUN_TEST(acl_output_pointers_are_request_offsets);
    RUN_TEST(fixed_request_bytes);
    RUN_TEST(privileged_flag_is_all_ones_when_super_count_is_positive);
    RUN_TEST(name_copy_is_32_bytes_and_ignores_name_len);
    RUN_TEST(one_zero_word_is_passed_three_times);
    RUN_TEST(get_re_sids_failure_returns_before_sending);
    RUN_TEST(get_proj_list_failure_returns_before_sending);
    RUN_TEST(send_failure_leaves_the_result_alone);
    RUN_TEST(reply_unpack);
    RUN_TEST(short_reply_zeroes_the_extra_longword);

    printf("\n  %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
