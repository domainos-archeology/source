/*
 * rem_file/test/test_request_layouts.c - the fixed request records of the
 * simple REM_FILE_$* client stubs.
 *
 * Eight builders are #included below and driven through one mocked
 * REM_FILE_$SEND_REQUEST that captures the request bytes, the declared request
 * length and every pointer argument.  The behaviours checked here are the ones
 * beads source-haly, source-duwh, source-o8fc and source-xutx found wrong:
 *
 *   - REM_FILE_$PURIFY declares 0x14, not 0x16 ("move.w #0x14,-(SP)" at
 *     0x00E622D6);
 *   - REM_FILE_$ACL_IMAGE hands argument 11 (bulk_len) its own cell A6-0x176
 *     ("pea (-0x176,A6)" at 0x00E627E8), distinct from the zero word A6-0x172
 *     that arguments 4 and 9 share - every other builder aliases all three;
 *   - REM_FILE_$GROW_AREA / $DELETE_AREA place their fields at the offsets the
 *     stores use (current_size +0x0C, area_handle +0x14, new_size +0x18;
 *     area_offset +0x10, area_handle +0x14), which a naturally aligned struct
 *     gets wrong under m68k-elf-gcc's 2-byte int alignment;
 *   - REM_FILE_$INVALIDATE's flags argument is a single byte, read with
 *     "move.b (0x18,A6),D0b" at 0x00E623EE and stored as one byte at +0x14.
 *
 * The layout asserts themselves are _Static_asserts inside the files under
 * test, so merely compiling this program exercises them; the runtime tests
 * below check that the builders actually store where those offsets say.
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
 * The mocked transport
 * ============================================================================ */

static int       send_calls;
static uint8_t   seen_request[0x80];
static int16_t   seen_request_len;
static uint16_t  seen_response_max;
static int16_t   seen_extra_len;
static int16_t   seen_bulk_max;
static void     *seen_addr_info;
static void     *seen_extra_data;
static void     *seen_bulk_data;
static void     *seen_received_len;
static void     *seen_bulk_len;
static void     *seen_packet_id;
static void     *seen_status;

/* Written through bulk_len by the mock, so a caller that aliases bulk_len
 * onto its zero word can be caught red-handed. */
#define MOCK_BULK_LEN_STORE  0x5A5A

void REM_FILE_$SEND_REQUEST(void *addr_info, void *request, int16_t request_len,
                            void *extra_data, int16_t extra_len,
                            void *response, uint16_t response_max,
                            uint16_t *received_len, void *bulk_data,
                            int16_t bulk_max, int16_t *bulk_len,
                            uint16_t *packet_id, status_$t *status_ret)
{
    send_calls++;
    seen_request_len  = request_len;
    seen_response_max = response_max;
    seen_extra_len    = extra_len;
    seen_bulk_max     = bulk_max;
    seen_addr_info    = addr_info;
    seen_extra_data   = extra_data;
    seen_bulk_data    = bulk_data;
    seen_received_len = received_len;
    seen_bulk_len     = bulk_len;
    seen_packet_id    = packet_id;
    seen_status       = status_ret;

    memset(seen_request, 0, sizeof(seen_request));
    memcpy(seen_request, request, (size_t)request_len);

    memset(response, 0, response_max);
    *received_len = 8;
    *bulk_len     = MOCK_BULK_LEN_STORE;
    *packet_id    = 0x1234;
    *status_ret   = status_$ok;
}

/* ============================================================================
 * The code under test
 * ============================================================================ */

#include "../purify.c"
#include "../acl_image.c"
#include "../grow_area.c"
#include "../delete_area.c"
#include "../invalidate.c"
#include "../set_def_acl.c"
#include "../local_verify.c"
#include "../set_attribute.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

/*
 * The builders store native words and longwords, so read them back the same
 * way; what these tests pin down is the OFFSET each store lands on, which is
 * byte-order independent.  memcpy keeps the reads aligned on hosts that care.
 */
static uint16_t req_w(int off)
{
    uint16_t v;
    memcpy(&v, &seen_request[off], sizeof(v));
    return v;
}

static uint32_t req_l(int off)
{
    uint32_t v;
    memcpy(&v, &seen_request[off], sizeof(v));
    return v;
}

static void reset(void)
{
    send_calls = 0;
    memset(seen_request, 0, sizeof(seen_request));
    seen_request_len = 0;
    memset(ACL_$UNWIRED_DATA.super_count, 0, sizeof(ACL_$UNWIRED_DATA.super_count));
    PROC1_$CURRENT = 3;
}

#define UID_HIGH  0x11223344u
#define UID_LOW   0x55667788u

static uid_t test_uid(uint32_t hi, uint32_t lo)
{
    uid_t u;
    u.high = hi;
    u.low  = lo;
    return u;
}

/* ============================================================================
 * REM_FILE_$PURIFY (0x00E6225C) - bead source-haly
 * ============================================================================ */

TEST(purify_declares_a_0x14_byte_request)
{
    uid_t vol = test_uid(1, 2);
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    uint16_t flags = 0xBEEF;
    status_$t st = 0;

    reset();
    REM_FILE_$PURIFY(&vol, &fil, &flags, 0x0102, &st);

    ASSERT_EQ(1, send_calls);
    /* "move.w #0x14,-(SP)" at 0x00E622D6 */
    ASSERT_EQ(0x14, seen_request_len);
    ASSERT_EQ(0x14, sizeof(rem_file_purify_req_t));
    ASSERT_EQ(REM_FILE_RESPONSE_BUF_SIZE, seen_response_max);
}

TEST(purify_field_offsets)
{
    uid_t vol = test_uid(1, 2);
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    uint16_t flags = 0xBEEF;
    status_$t st = 0;

    reset();
    ACL_$UNWIRED_DATA.super_count[3] = 1;          /* PROC1_$CURRENT = 3 */
    REM_FILE_$PURIFY(&vol, &fil, &flags, 0x0102, &st);

    ASSERT_EQ(REM_FILE_REQ_MAGIC, seen_request[0x02]);   /* 0x00E6226C */
    ASSERT_EQ(REM_FILE_OP_PURIFY, seen_request[0x03]);   /* 0x00E62272 */
    ASSERT_EQ(UID_HIGH, req_l(0x04));                    /* 0x00E6227C */
    ASSERT_EQ(UID_LOW,  req_l(0x08));                    /* 0x00E62280 */
    ASSERT_EQ(0xBEEF,   req_w(0x0C));                    /* 0x00E62288 */
    ASSERT_EQ(0x0102,   req_w(0x0E));                    /* 0x00E6228C */
    ASSERT_EQ(3,        req_w(0x10));                    /* 0x00E62290 */
    ASSERT_EQ(0xFF,     seen_request[0x12]);             /* sgt, 0x00E622AA */
}

TEST(purify_admin_flag_is_false_without_super_count)
{
    uid_t vol = test_uid(1, 2);
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    uint16_t flags = 0;
    status_$t st = 0;

    reset();
    ACL_$UNWIRED_DATA.super_count[3] = 0;
    REM_FILE_$PURIFY(&vol, &fil, &flags, 0, &st);
    ASSERT_EQ(0x00, seen_request[0x12]);
}

TEST(purify_aliases_the_zero_word_onto_arguments_4_9_and_11)
{
    uid_t vol = test_uid(1, 2);
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    uint16_t flags = 0;
    status_$t st = 0;

    reset();
    REM_FILE_$PURIFY(&vol, &fil, &flags, 0, &st);

    /* All three "pea (-0x172,A6)": 0x00E622D2, 0x00E622C0, 0x00E622BA */
    ASSERT_EQ(1, seen_extra_data == seen_bulk_data);
    ASSERT_EQ(1, seen_extra_data == (void *)seen_bulk_len);
    /* ...and the received_len / packet_id cells are their own */
    ASSERT_EQ(1, seen_received_len != seen_extra_data);
    ASSERT_EQ(1, seen_packet_id != seen_extra_data);
    ASSERT_EQ(1, seen_packet_id != seen_received_len);
}

/* ============================================================================
 * REM_FILE_$ACL_IMAGE (0x00E627A8) - bead source-duwh
 * ============================================================================ */

TEST(acl_image_request_layout)
{
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    uint8_t image[0x400];
    uint16_t acl_len = 0;
    uint32_t header[REM_FILE_ACL_IMAGE_HEADER_LONGS];
    status_$t st = 0;
    int addr_info = 0;

    reset();
    REM_FILE_$ACL_IMAGE(&addr_info, &fil, 0x7B, image, &acl_len, header, &st);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(0x14, seen_request_len);                   /* 0x00E62806 */
    ASSERT_EQ(0x14, sizeof(rem_file_acl_image_req_t));
    ASSERT_EQ(REM_FILE_REQ_MAGIC,    seen_request[0x02]);
    ASSERT_EQ(REM_FILE_OP_ACL_IMAGE, seen_request[0x03]);
    ASSERT_EQ(UID_HIGH, req_l(0x04));
    ASSERT_EQ(UID_LOW,  req_l(0x08));
    ASSERT_EQ(5,        req_w(0x0C));                    /* 0x00E627D6 */
    ASSERT_EQ(0x7B,     seen_request[0x0E]);             /* 0x00E627D2 */
    /* The bulk arguments: the caller's buffer (argument 9, "move.l (0x12,A6)"
     * at 0x00E627F0 - not the zero word every other builder passes) and the
     * 0x400 ceiling. */
    ASSERT_EQ(1, seen_bulk_data == (void *)image);
    ASSERT_EQ(REM_FILE_ACL_IMAGE_BULK_MAX, seen_bulk_max); /* 0x00E627EC */
}

TEST(acl_image_gives_bulk_len_its_own_cell)
{
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    uint8_t image[0x400];
    uint16_t acl_len = 0;
    uint32_t header[REM_FILE_ACL_IMAGE_HEADER_LONGS];
    status_$t st = 0;
    int addr_info = 0;

    reset();
    REM_FILE_$ACL_IMAGE(&addr_info, &fil, 0, image, &acl_len, header, &st);

    /*
     * Four distinct word cells: A6-0x178 received_len (0x00E627F4),
     * A6-0x176 bulk_len (0x00E627E8), A6-0x174 packet_id (0x00E627E4) and the
     * zero word A6-0x172 (0x00E627DC) that arguments 4 and 9 share.
     */
    ASSERT_EQ(1, seen_bulk_data == (void *)image);
    ASSERT_EQ(1, seen_extra_data != seen_bulk_data);
    ASSERT_EQ(1, (void *)seen_bulk_len != seen_extra_data);
    ASSERT_EQ(1, (void *)seen_bulk_len != seen_received_len);
    ASSERT_EQ(1, (void *)seen_bulk_len != seen_packet_id);

    /* The transport's store through bulk_len must not reach the extra-length
     * word the same call passes as argument 4. */
    ASSERT_EQ(0, *(uint16_t *)seen_extra_data);
    ASSERT_EQ(0, seen_extra_len);
}

TEST(acl_image_copies_the_reply_header_out)
{
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    uint8_t image[0x400];
    uint16_t acl_len = 0;
    uint32_t header[REM_FILE_ACL_IMAGE_HEADER_LONGS];
    status_$t st = 0;
    int addr_info = 0;

    reset();
    memset(header, 0xAA, sizeof(header));
    REM_FILE_$ACL_IMAGE(&addr_info, &fil, 0, image, &acl_len, header, &st);

    /* The mock zeroes the reply, so both outputs come back zero - what this
     * proves is that eleven longwords are written, not ten or twelve. */
    ASSERT_EQ(0, acl_len);
    ASSERT_EQ(0, header[0]);
    ASSERT_EQ(0, header[REM_FILE_ACL_IMAGE_HEADER_LONGS - 1]);
    ASSERT_EQ(0x0A, __builtin_offsetof(rem_file_acl_image_resp_t, acl_len));
    ASSERT_EQ(0x0C, __builtin_offsetof(rem_file_acl_image_resp_t, acl_header));
}

/* ============================================================================
 * REM_FILE_$GROW_AREA (0x00E62734) / $DELETE_AREA (0x00E626CC) - source-o8fc
 * ============================================================================ */

TEST(grow_area_request_layout)
{
    status_$t st = 0;
    int addr_info = 0;

    reset();
    REM_FILE_$GROW_AREA(&addr_info, 0xCAFE, 0x11112222u, 0x33334444u, &st);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(0x1C, seen_request_len);                   /* 0x00E6278E */
    ASSERT_EQ(0x1C, sizeof(rem_file_grow_area_req_t));
    ASSERT_EQ(REM_FILE_REQ_MAGIC,     seen_request[0x02]);
    ASSERT_EQ(REM_FILE_OP_GROW_AREA,  seen_request[0x03]);
    ASSERT_EQ(0x11112222u, req_l(0x0C));                 /* 0x00E6275E */
    ASSERT_EQ(0xCAFE,      req_w(0x14));                 /* 0x00E6275A */
    ASSERT_EQ(0x33334444u, req_l(0x18));                 /* 0x00E62762 */
}

TEST(delete_area_request_layout)
{
    status_$t st = 0;
    int addr_info = 0;

    reset();
    REM_FILE_$DELETE_AREA(&addr_info, 0xBEEF, 0x0A0B0C0Du, &st);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(0x1C, seen_request_len);                   /* 0x00E6271C */
    ASSERT_EQ(0x1C, sizeof(rem_file_delete_area_req_t));
    ASSERT_EQ(REM_FILE_REQ_MAGIC,      seen_request[0x02]);
    ASSERT_EQ(REM_FILE_OP_DELETE_AREA, seen_request[0x03]);
    ASSERT_EQ(0x0A0B0C0Du, req_l(0x10));                 /* 0x00E626F0 */
    ASSERT_EQ(0xBEEF,      req_w(0x14));                 /* 0x00E626EC */
}

/* ============================================================================
 * REM_FILE_$INVALIDATE (0x00E623D8) - bead source-xutx
 * ============================================================================ */

TEST(invalidate_request_layout)
{
    uid_t vol = test_uid(1, 2);
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    status_$t st = 0;

    reset();
    REM_FILE_$INVALIDATE(&vol, &fil, 0x00010002u, 0x00030004u, true, &st);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(0x16, seen_request_len);                   /* 0x00E6243E */
    ASSERT_EQ(0x16, sizeof(rem_file_invalidate_req_t));
    ASSERT_EQ(REM_FILE_REQ_MAGIC,     seen_request[0x02]);
    ASSERT_EQ(REM_FILE_OP_INVALIDATE, seen_request[0x03]);
    ASSERT_EQ(UID_HIGH,    req_l(0x04));
    ASSERT_EQ(UID_LOW,     req_l(0x08));
    ASSERT_EQ(0x00010002u, req_l(0x0C));                 /* 0x00E6240A */
    ASSERT_EQ(0x00030004u, req_l(0x10));                 /* 0x00E6240E */
    /* One byte at +0x14 ("move.b D0b,(-0x15c,A6)" at 0x00E62412) */
    ASSERT_EQ(0xFF, seen_request[0x14]);
}

TEST(invalidate_flags_is_a_single_byte)
{
    uid_t vol = test_uid(1, 2);
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    status_$t st = 0;

    reset();
    REM_FILE_$INVALIDATE(&vol, &fil, 0, 0, false, &st);
    ASSERT_EQ(0x00, seen_request[0x14]);
    ASSERT_EQ(1, sizeof(((rem_file_invalidate_req_t *)0)->flags));
}

/* ============================================================================
 * The remaining fixed-length builders (sweep for bead source-o8fc)
 * ============================================================================ */

TEST(set_def_acl_request_layout)
{
    uid_t dir   = test_uid(0x01010101u, 0x02020202u);
    uid_t acl   = test_uid(0x03030303u, 0x04040404u);
    uid_t owner = test_uid(0x05050505u, 0x06060606u);
    status_$t st = 0;
    int vol = 0;

    reset();
    REM_FILE_$SET_DEF_ACL(&vol, &dir, &acl, &owner, &st);

    ASSERT_EQ(0x20, seen_request_len);                   /* 0x00E6235C */
    ASSERT_EQ(0x20, sizeof(rem_file_set_def_acl_req_t));
    ASSERT_EQ(REM_FILE_OP_SET_DEF_ACL, seen_request[0x03]);
    ASSERT_EQ(0x01010101u, req_l(0x04));
    ASSERT_EQ(0x03030303u, req_l(0x0C));
    ASSERT_EQ(0x05050505u, req_l(0x14));
    ASSERT_EQ(3,    req_w(0x1C));                        /* 0x00E6232A */
    ASSERT_EQ(0xFF, seen_request[0x1E]);                 /* "st", 0x00E62330 */
}

TEST(local_verify_request_layout)
{
    uint32_t lock_block[16];
    status_$t st = 0;
    int addr_info = 0;
    int i;

    reset();
    for (i = 0; i < 16; i++) {
        lock_block[i] = 0xA0000000u + (uint32_t)i;
    }

    REM_FILE_$LOCAL_VERIFY(&addr_info, lock_block, &st);

    ASSERT_EQ(0x2E, seen_request_len);                   /* 0x00E61E80 */
    ASSERT_EQ(0x2E, sizeof(rem_file_local_verify_req_t));
    ASSERT_EQ(REM_FILE_OP_LOCAL_VERIFY, seen_request[0x03]);
    /* +0x04: the first two longwords of the lock block (0x00E61E40) */
    ASSERT_EQ(0xA0000000u, req_l(0x04));
    ASSERT_EQ(0xA0000001u, req_l(0x08));
    /* +0x0C: A1 is reset to the head of the block (0x00E61E48), so the UID is
     * copied a second time here. */
    ASSERT_EQ(0xA0000000u, req_l(0x0C));
    ASSERT_EQ(0xA0000007u, req_l(0x28));
    /* +0x2C: the next two bytes of the stream, i.e. the first two bytes of
     * lock_block[8] (0x00E61E56). */
    ASSERT_EQ(((const uint8_t *)lock_block)[32], seen_request[0x2C]);
    ASSERT_EQ(((const uint8_t *)lock_block)[33], seen_request[0x2D]);
}

TEST(set_attribute_request_layout)
{
    uid_t fil = test_uid(UID_HIGH, UID_LOW);
    uint32_t attr[REM_FILE_SET_ATTRIBUTE_DATA_LONGS];
    status_$t st = 0;
    int vol = 0;
    int i;

    reset();
    for (i = 0; i < REM_FILE_SET_ATTRIBUTE_DATA_LONGS; i++) {
        attr[i] = 0xB0000000u + (uint32_t)i;
    }

    REM_FILE_$SET_ATTRIBUTE(&vol, &fil, 0x0055, attr, &st);

    ASSERT_EQ(0x42, seen_request_len);                   /* 0x00E61A96 */
    ASSERT_EQ(0x42, sizeof(rem_file_set_attr_req_t));
    ASSERT_EQ(REM_FILE_OP_SET_ATTRIBUTE, seen_request[0x03]);
    ASSERT_EQ(UID_HIGH, req_l(0x04));
    ASSERT_EQ(UID_LOW,  req_l(0x08));
    ASSERT_EQ(0x0055,   req_w(0x0C));                    /* 0x00E61A5A */
    ASSERT_EQ(0xB0000000u, req_l(0x0E));                 /* 0x00E61A68 */
    ASSERT_EQ(0xB000000Cu, req_l(0x3E));                 /* the 13th long */
}

/* ============================================================================ */

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("REM_FILE request-record layout tests\n");

    RUN_TEST(purify_declares_a_0x14_byte_request);
    RUN_TEST(purify_field_offsets);
    RUN_TEST(purify_admin_flag_is_false_without_super_count);
    RUN_TEST(purify_aliases_the_zero_word_onto_arguments_4_9_and_11);

    RUN_TEST(acl_image_request_layout);
    RUN_TEST(acl_image_gives_bulk_len_its_own_cell);
    RUN_TEST(acl_image_copies_the_reply_header_out);

    RUN_TEST(grow_area_request_layout);
    RUN_TEST(delete_area_request_layout);

    RUN_TEST(invalidate_request_layout);
    RUN_TEST(invalidate_flags_is_a_single_byte);

    RUN_TEST(set_def_acl_request_layout);
    RUN_TEST(local_verify_request_layout);
    RUN_TEST(set_attribute_request_layout);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
