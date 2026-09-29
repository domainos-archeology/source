/*
 * rem_file/test/test_neighbors.c - unit tests for REM_FILE_$NEIGHBORS
 * (0x00E621B8)
 *
 * Covers what the 2026-09-07 re-emission corrected (bead source-vaov):
 *   - the reply length handed to REM_FILE_$SEND_REQUEST is 0xBE
 *     (`move.w #0xbe,-(SP)` at 0x00E62224), not 0xE4
 *   - the answer is the byte at response+0x08 (A6-0xB8, buffer at A6-0xC0),
 *     not response+0x04
 *   - a non-zero status returns 0 without reading the reply (0x00E62246)
 *   - the 0x18-byte request layout
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
 * ============================================================================ */

#include "../neighbors.c"

/* ============================================================================
 * Globals and mocks
 * ============================================================================ */

uint16_t PROC1_$CURRENT;
MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);

static rem_file_$neighbors_req_t  snd_request;
static int16_t   snd_request_len;
static uint16_t  snd_response_max;
static void     *snd_extra_data;
static void     *snd_bulk_data;
static int16_t  *snd_bulk_len;
static int16_t   snd_bulk_max;
static int16_t   snd_extra_len;
static status_$t snd_status_out;
static int8_t    snd_reply_byte;
static int       snd_calls;

void REM_FILE_$SEND_REQUEST(void *addr_info, void *request, int16_t request_len,
                            void *extra_data, int16_t extra_len,
                            void *response, uint16_t response_max,
                            uint16_t *received_len, void *bulk_data,
                            int16_t bulk_max, int16_t *bulk_len,
                            uint16_t *packet_id, status_$t *status_ret)
{
    rem_file_$neighbors_resp_t *resp = (rem_file_$neighbors_resp_t *)response;

    (void)addr_info;

    snd_calls++;
    memcpy(&snd_request, request, sizeof(snd_request));
    snd_request_len  = request_len;
    snd_response_max = response_max;
    snd_extra_data   = extra_data;
    snd_extra_len    = extra_len;
    snd_bulk_data    = bulk_data;
    snd_bulk_max     = bulk_max;
    snd_bulk_len     = bulk_len;

    memset(resp, 0, sizeof(*resp));
    resp->are_neighbors = snd_reply_byte;

    *received_len = 0x0A;
    *packet_id    = 0x2222;
    *status_ret   = snd_status_out;
}

/* ============================================================================
 * Fixtures
 * ============================================================================ */

static uint32_t addr_info[2];
static uid_t    uid1;
static uid_t    uid2;
static status_$t st;

static void reset(void)
{
    memset(&snd_request, 0, sizeof(snd_request));
    memset(ACL_$UNWIRED_DATA.super_count, 0, sizeof(ACL_$UNWIRED_DATA.super_count));
    PROC1_$CURRENT = 2;
    snd_status_out = status_$ok;
    snd_reply_byte = 0;
    snd_calls = 0;
    st = 0x7FFFFFFF;

    uid1.high = 0x11111111u; uid1.low = 0x22222222u;
    uid2.high = 0x33333333u; uid2.low = 0x44444444u;
}

static int8_t call(void)
{
    return REM_FILE_$NEIGHBORS(addr_info, &uid1, &uid2, &st);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(request_layout_and_length)
{
    reset();
    (void)call();

    ASSERT_EQ(0x18, snd_request_len);
    ASSERT_EQ(REM_FILE_REQ_MAGIC, snd_request.magic);
    ASSERT_EQ(REM_FILE_OP_NEIGHBORS, snd_request.opcode);
    ASSERT_EQ(0x11111111u, snd_request.uid1.high);
    ASSERT_EQ(0x22222222u, snd_request.uid1.low);
    ASSERT_EQ(0x33333333u, snd_request.uid2.high);
    ASSERT_EQ(0x44444444u, snd_request.uid2.low);
    ASSERT_EQ(3, snd_request.reserved);
    ASSERT_EQ(0, (uint8_t)snd_request.admin_flag);
}

TEST(admin_flag_follows_acl_super_count)
{
    reset();
    ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT] = 1;
    (void)call();
    ASSERT_EQ(0xFF, (uint8_t)snd_request.admin_flag);
}

TEST(response_length_is_0xBE)
{
    reset();
    (void)call();
    ASSERT_EQ(0xBE, snd_response_max);
    ASSERT_EQ(REM_FILE_RESPONSE_BUF_SIZE, snd_response_max);
}

TEST(one_cleared_word_serves_three_arguments)
{
    reset();
    (void)call();
    /* extra_data, bulk_data and bulk_len are all the same cell
     * (`pea (-0x172,A6)` three times) */
    ASSERT_EQ((unsigned long)(size_t)snd_extra_data,
              (unsigned long)(size_t)snd_bulk_data);
    ASSERT_EQ((unsigned long)(size_t)snd_extra_data,
              (unsigned long)(size_t)snd_bulk_len);
    ASSERT_EQ(0, snd_extra_len);
    ASSERT_EQ(0, snd_bulk_max);
}

TEST(result_comes_from_response_plus_8)
{
    reset();
    snd_reply_byte = (int8_t)0xFF;
    ASSERT_EQ(0xFF, (uint8_t)call());

    reset();
    snd_reply_byte = 0x01;
    ASSERT_EQ(0x01, (uint8_t)call());
}

TEST(a_bad_status_returns_zero)
{
    reset();
    snd_reply_byte = (int8_t)0xFF;
    snd_status_out = 0x000F0004;
    ASSERT_EQ(0, (uint8_t)call());
    ASSERT_EQ(0x000F0004, st);
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void)
{
    printf("REM_FILE_$NEIGHBORS tests\n");

    RUN_TEST(request_layout_and_length);
    RUN_TEST(admin_flag_follows_acl_super_count);
    RUN_TEST(response_length_is_0xBE);
    RUN_TEST(one_cleared_word_serves_three_arguments);
    RUN_TEST(result_comes_from_response_plus_8);
    RUN_TEST(a_bad_status_returns_zero);

    printf("\n%d tests, %d failures\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
