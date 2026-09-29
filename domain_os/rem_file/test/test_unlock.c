/*
 * rem_file/test/test_unlock.c - unit tests for REM_FILE_$UNLOCK (0x00E61D1C)
 *
 * The real rem_file/unlock.c is #included below and driven through mocked
 * REM_FILE_$SEND_REQUEST / AST_$SET_DTS.  The behaviours exercised are the
 * ones bead source-zm7e found wrong:
 *
 *   - the 0x22-byte request record (flags/version at +0x18, the super-mode
 *     boolean at +0x1A, the lock key at +0x1C and the release byte at +0x20);
 *   - the response being read at offset 0 of the buffer rather than at
 *     `bufsize - sizeof(response)`;
 *   - the two reply-length gates (`cmpi.w #0x8` at 0x00E61DCC and
 *     `cmpi.w #0x16` at 0x00E61DE6), both signed word compares;
 *   - the AST_$SET_DTS selector (0x02 from the reply's dtv, 0x08 only when
 *     the caller passed a true release flag AND the server reported valid
 *     attributes AND the reply is long enough);
 *   - that the returned byte is response+0x0E.
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
        printf("  %-46s ", #name);                                            \
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
 * Globals the code under test references
 * ============================================================================ */

MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
uint16_t PROC1_$CURRENT;

/* ============================================================================
 * Mocks
 * ============================================================================ */

/* What the mock REM_FILE_$SEND_REQUEST saw. */
static int       send_calls;
static void     *seen_addr_info;
static uint8_t   seen_request[0x40];
static int16_t   seen_request_len;
static uint16_t  seen_response_max;
static int16_t   seen_extra_len;
static int16_t   seen_bulk_max;
static void     *seen_extra_data;
static void     *seen_bulk_data;
static void     *seen_bulk_len;

/* What the mock hands back. */
static uint8_t   canned_reply[0x40];
static uint16_t  canned_reply_len;
static status_$t canned_status;

void REM_FILE_$SEND_REQUEST(void *addr_info, void *request, int16_t request_len,
                            void *extra_data, int16_t extra_len,
                            void *response, uint16_t response_max,
                            uint16_t *received_len, void *bulk_data,
                            int16_t bulk_max, int16_t *bulk_len,
                            uint16_t *packet_id, status_$t *status_ret)
{
    send_calls++;
    seen_addr_info    = addr_info;
    seen_request_len  = request_len;
    seen_response_max = response_max;
    seen_extra_len    = extra_len;
    seen_bulk_max     = bulk_max;
    seen_extra_data   = extra_data;
    seen_bulk_data    = bulk_data;
    seen_bulk_len     = bulk_len;
    memcpy(seen_request, request, (size_t)request_len);

    memset(response, 0xEE, response_max);
    memcpy(response, canned_reply, sizeof(canned_reply));
    *received_len = canned_reply_len;
    *packet_id    = 0x1234;
    *status_ret   = canned_status;
}

/* What the mock AST_$SET_DTS saw. */
static int       dts_calls;
static uint16_t  dts_flags;
static uid_t    *dts_uid;
static uint32_t *dts_dtv;
static uint32_t *dts_dtu;
static status_$t *dts_status_ret;

uint8_t AST_$SET_DTS(uint16_t flags, uid_t *uid, uint32_t *dtv,
                     uint32_t *access_time, status_$t *status)
{
    dts_calls++;
    dts_flags      = flags;
    dts_uid        = uid;
    dts_dtv        = dtv;
    dts_dtu        = access_time;
    dts_status_ret = status;
    *status = status_$ok;
    return 0;
}

/* ============================================================================
 * The code under test
 * ============================================================================ */

#include "../unlock.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

#define TEST_UID_HIGH   0x11223344u
#define TEST_UID_LOW    0x55667788u

static file_$obj_loc_t desc;

static void reset(void)
{
    memset(&desc, 0, sizeof(desc));
    desc.uid.high = TEST_UID_HIGH;
    desc.uid.low  = TEST_UID_LOW;
    desc.loc_info = 0xAABBCCDDu;
    desc.node     = 0x000A0B0Cu;

    memset(seen_request, 0, sizeof(seen_request));
    memset(canned_reply, 0, sizeof(canned_reply));
    send_calls = 0;
    dts_calls  = 0;
    dts_flags  = 0;
    canned_reply_len = 0x16;
    canned_status    = status_$ok;

    PROC1_$CURRENT = 7;
    memset(ACL_$UNWIRED_DATA.super_count, 0, sizeof(ACL_$UNWIRED_DATA.super_count));
}

/*
 * Field accessors into the captured request, by raw image offset.  The
 * multi-byte ones go through memcpy in the host's own byte order, which is
 * exactly what the struct member does - so what they check is the *offset*,
 * and they behave the same on a little-endian host as on the m68k.
 */
static uint8_t  rq_b(int off) { return seen_request[off]; }
static uint16_t rq_w(int off)
{
    uint16_t v; memcpy(&v, &seen_request[off], sizeof(v)); return v;
}
static uint32_t rq_l(int off)
{
    uint32_t v; memcpy(&v, &seen_request[off], sizeof(v)); return v;
}

/* Write into the canned reply by raw image offset. */
static void rp_b(int off, uint8_t v) { canned_reply[off] = v; }
static void rp_l(int off, uint32_t v)
{
    memcpy(&canned_reply[off], &v, sizeof(v));
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/*
 * The 0x22-byte request record.  These offsets come straight from the writes
 * at 0x00E61D42-0x00E61D88; the length is `move.w #0x22,-(SP)` at 0x00E61DB4.
 */
TEST(request_record_layout)
{
    ASSERT_EQ(0x02, offsetof(rem_file_unlock_req_t, magic));
    ASSERT_EQ(0x03, offsetof(rem_file_unlock_req_t, opcode));
    ASSERT_EQ(0x04, offsetof(rem_file_unlock_req_t, file_uid));
    ASSERT_EQ(0x0C, offsetof(rem_file_unlock_req_t, rem_key));
    ASSERT_EQ(0x10, offsetof(rem_file_unlock_req_t, rem_node));
    ASSERT_EQ(0x14, offsetof(rem_file_unlock_req_t, lock_mode));
    ASSERT_EQ(0x18, offsetof(rem_file_unlock_req_t, version));
    ASSERT_EQ(0x1A, offsetof(rem_file_unlock_req_t, super_user));
    ASSERT_EQ(0x1C, offsetof(rem_file_unlock_req_t, lock_key));
    ASSERT_EQ(0x20, offsetof(rem_file_unlock_req_t, release));
    ASSERT_EQ(0x21, offsetof(rem_file_unlock_req_t, uninit_21));
    /* The wire length is the literal 0x22 the image pushes at 0x00E61DB4, not
     * `sizeof` - a 64-bit host pads the record to 0x24. */
    ASSERT_EQ(0x22, REM_FILE_UNLOCK_REQ_LEN);
    ASSERT_EQ(1, sizeof(rem_file_unlock_req_t) >= REM_FILE_UNLOCK_REQ_LEN);
}

/*
 * The reply record: FILE_$PRIV_UNLOCK's dtv at +0x08, its result byte at
 * +0x0E, the "attributes are valid" boolean at +0x0F and the access time at
 * +0x10 - exactly what rem_file/server.c's 0x0C case writes.
 */
TEST(response_record_layout)
{
    ASSERT_EQ(0x04, offsetof(rem_file_unlock_resp_t, status));
    ASSERT_EQ(0x08, offsetof(rem_file_unlock_resp_t, dtv_high));
    ASSERT_EQ(0x0C, offsetof(rem_file_unlock_resp_t, dtv_low));
    ASSERT_EQ(0x0E, offsetof(rem_file_unlock_resp_t, result));
    ASSERT_EQ(0x0F, offsetof(rem_file_unlock_resp_t, attrs_valid));
    ASSERT_EQ(0x10, offsetof(rem_file_unlock_resp_t, dtu_high));
    ASSERT_EQ(0x14, offsetof(rem_file_unlock_resp_t, dtu_low));
}

/* Every field the image writes, at the image's offsets. */
TEST(request_is_built_at_the_image_offsets)
{
    status_$t st = 0;

    reset();
    (void)REM_FILE_$UNLOCK(&desc, 0x0605, 0x0A0B0C0Du, 0x0708,
                           0x01020304u, false, &st);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(0x22, seen_request_len);
    ASSERT_EQ(0xBE, seen_response_max);

    ASSERT_EQ(0x80, rq_b(0x02));                /* 0x00E61D42 */
    ASSERT_EQ(0x0C, rq_b(0x03));                /* 0x00E61D48 */
    ASSERT_EQ(TEST_UID_HIGH, rq_l(0x04));       /* 0x00E61D52 */
    ASSERT_EQ(TEST_UID_LOW,  rq_l(0x08));       /* 0x00E61D56 */
    ASSERT_EQ(0x0A0B0C0Du,   rq_l(0x0C));       /* 0x00E61D5A */
    ASSERT_EQ(0x01020304u,   rq_l(0x10));       /* 0x00E61D5E */
    ASSERT_EQ(0x0605,        rq_w(0x14));       /* 0x00E61D66 */
    ASSERT_EQ(3,             rq_w(0x18));       /* 0x00E61D6A */
    ASSERT_EQ(0x0708,        rq_w(0x1C));       /* 0x00E61D62 */
    ASSERT_EQ(0x00,          rq_b(0x20));       /* 0x00E61D88 */
}

/*
 * `pea (0x10,A2)` at 0x00E61DBC: the address info handed to SEND_REQUEST is
 * the descriptor's loc_info/node pair, not the descriptor itself.
 */
TEST(addr_info_is_location_block_plus_0x10)
{
    status_$t st = 0;

    reset();
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);

    ASSERT_EQ((unsigned long)((uint8_t *)&desc + 0x10),
              (unsigned long)seen_addr_info);
}

/*
 * 0x00E61D8C clears one word and 0x00E61D98 / 0x00E61DA2 / 0x00E61DB0 pass
 * its address three times; the two length arguments are immediate zeros.
 */
TEST(extra_and_bulk_share_one_zero_word)
{
    status_$t st = 0;

    reset();
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);

    ASSERT_EQ(0, seen_extra_len);
    ASSERT_EQ(0, seen_bulk_max);
    ASSERT_EQ((unsigned long)seen_extra_data, (unsigned long)seen_bulk_data);
    ASSERT_EQ((unsigned long)seen_extra_data, (unsigned long)seen_bulk_len);
}

/* `sgt D5b` at 0x00E61D82: strictly positive -> 0xFF, otherwise 0. */
TEST(super_user_byte_tracks_acl_super_count)
{
    status_$t st = 0;

    reset();
    ACL_$UNWIRED_DATA.super_count[7] = 0;
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);
    ASSERT_EQ(0x00, rq_b(0x1A));

    reset();
    ACL_$UNWIRED_DATA.super_count[7] = 1;
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);
    ASSERT_EQ(0xFF, rq_b(0x1A));

    /* The count is indexed by PROC1_$CURRENT, so another slot must not
     * change the answer. */
    reset();
    ACL_$UNWIRED_DATA.super_count[8] = 5;
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);
    ASSERT_EQ(0x00, rq_b(0x1A));
}

/* `st -(SP)` on the caller's side puts 0xFF in the byte slot. */
TEST(release_flag_reaches_offset_0x20)
{
    status_$t st = 0;

    reset();
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(0xFF, rq_b(0x20));
}

/* `cmpi.w #0x8,D0w; ble` at 0x00E61DCC -> `clr.b D0b` at 0x00E61E14. */
TEST(short_reply_returns_zero_and_skips_set_dts)
{
    status_$t st = 0;
    uint8_t   r;

    reset();
    canned_reply_len = 8;
    rp_b(0x0E, 0x5A);
    rp_l(0x08, 0x11111111u);        /* would otherwise select flag 0x02 */

    r = REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, dts_calls);
}

/* The compare is signed, so a 0xFFFF length is "short", not "very long". */
TEST(reply_length_compare_is_signed)
{
    status_$t st = 0;
    uint8_t   r;

    reset();
    canned_reply_len = 0xFFFF;
    rp_b(0x0E, 0x5A);
    rp_b(0x0F, 0xFF);
    rp_l(0x10, 0x22222222u);

    r = REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, dts_calls);
}

/* Nine bytes is the first length that carries a payload. */
TEST(reply_of_nine_returns_the_result_byte)
{
    status_$t st = 0;
    uint8_t   r;

    reset();
    canned_reply_len = 9;
    rp_b(0x0E, 0x5A);

    r = REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);
    ASSERT_EQ(0x5A, r);
    ASSERT_EQ(0, dts_calls);        /* dtv still zero, release false */
}

/* `tst.l (-0xb8,A6); beq` at 0x00E61DD4: only the HIGH longword is tested. */
TEST(nonzero_dtv_selects_flag_2)
{
    status_$t st = 0;

    reset();
    canned_reply_len = 9;
    rp_l(0x08, 0x00000001u);

    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);
    ASSERT_EQ(1, dts_calls);
    ASSERT_EQ(0x0002, dts_flags);
}

/*
 * 0x00E61DDC-0x00E61DEC: three conditions, all required.  The reply here is
 * long enough and the server said the attributes are valid, so only the
 * release flag decides.
 */
TEST(flag_8_needs_release_attrs_valid_and_length)
{
    status_$t st = 0;

    /* all three satisfied */
    reset();
    canned_reply_len = 0x16;
    rp_b(0x0F, 0xFF);
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(1, dts_calls);
    ASSERT_EQ(0x0008, dts_flags);

    /* release flag false */
    reset();
    canned_reply_len = 0x16;
    rp_b(0x0F, 0xFF);
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);
    ASSERT_EQ(0, dts_calls);

    /* attrs_valid not negative (0x7F is "set but positive") */
    reset();
    canned_reply_len = 0x16;
    rp_b(0x0F, 0x7F);
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(0, dts_calls);

    /* reply one byte too short for the access time at +0x10 */
    reset();
    canned_reply_len = 0x15;
    rp_b(0x0F, 0xFF);
    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(0, dts_calls);
}

/* `ori.w #0x8,D3w` at 0x00E61DEC ORs into the 0x02 already there. */
TEST(both_flags_combine)
{
    status_$t st = 0;

    reset();
    canned_reply_len = 0x16;
    rp_l(0x08, 0x99999999u);
    rp_b(0x0F, 0xFF);

    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(1, dts_calls);
    ASSERT_EQ(0x000A, dts_flags);
}

/*
 * `pea (0x8,A2)` / `pea (-0xb8,A6)` / `pea (-0xb0,A6)` at
 * 0x00E61DF6-0x00E61E02: the UID comes from the descriptor and the two clock
 * pointers from the reply, at +0x08 and +0x10 of the buffer's *start*.
 */
TEST(set_dts_gets_descriptor_uid_and_reply_clocks)
{
    status_$t st = 0;

    reset();
    canned_reply_len = 0x16;
    rp_l(0x08, 0x99999999u);
    rp_b(0x0F, 0xFF);

    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(1, dts_calls);
    ASSERT_EQ((unsigned long)&desc.uid, (unsigned long)dts_uid);
    /* The two clock pointers are 8 bytes apart inside one response record. */
    ASSERT_EQ(8, (uint8_t *)dts_dtu - (uint8_t *)dts_dtv);
    /* AST_$SET_DTS' status goes to a local, never to the caller's status. */
    ASSERT_EQ(0, (unsigned long)(dts_status_ret == &st));
}

/* The reply's result byte is at +0x0E of the buffer, not near its end. */
TEST(result_byte_comes_from_response_offset_0x0e)
{
    status_$t st = 0;
    uint8_t   r;

    reset();
    canned_reply_len = 0x16;
    rp_b(0x0E, 0xC3);
    rp_b(0x0F, 0xFF);

    r = REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, true, &st);
    ASSERT_EQ(0xC3, r);
}

/* The network status is SEND_REQUEST's, passed straight through. */
TEST(status_is_the_send_request_status)
{
    status_$t st = 0x5A5A5A5A;

    reset();
    canned_status    = (status_$t)0x000F0004;
    canned_reply_len = 0;

    (void)REM_FILE_$UNLOCK(&desc, 0, 0, 0, 0, false, &st);
    ASSERT_EQ((status_$t)0x000F0004, st);
}

int main(void)
{
    printf("REM_FILE_$UNLOCK (0x00E61D1C) tests\n");

    RUN_TEST(request_record_layout);
    RUN_TEST(response_record_layout);
    RUN_TEST(request_is_built_at_the_image_offsets);
    RUN_TEST(addr_info_is_location_block_plus_0x10);
    RUN_TEST(extra_and_bulk_share_one_zero_word);
    RUN_TEST(super_user_byte_tracks_acl_super_count);
    RUN_TEST(release_flag_reaches_offset_0x20);
    RUN_TEST(short_reply_returns_zero_and_skips_set_dts);
    RUN_TEST(reply_length_compare_is_signed);
    RUN_TEST(reply_of_nine_returns_the_result_byte);
    RUN_TEST(nonzero_dtv_selects_flag_2);
    RUN_TEST(flag_8_needs_release_attrs_valid_and_length);
    RUN_TEST(both_flags_combine);
    RUN_TEST(set_dts_gets_descriptor_uid_and_reply_clocks);
    RUN_TEST(result_byte_comes_from_response_offset_0x0e);
    RUN_TEST(status_is_the_send_request_status);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
