/*
 * rem_file/test/test_rn_do_op.c - unit tests for REM_FILE_$RN_DO_OP
 * (0x00E61538)
 *
 * Covers what the 2026-09-07 re-emission corrected (bead source-0i5f):
 *   - the 0x58 (DIR_$SERVER "list") arm takes its length from the WORD at
 *     request+0x92 and its source pointer from the LONG at request+0x8E
 *     (0x00E615D6 / 0x00E615FC / 0x00E61608), both ways round the 0x122 cap
 *   - the two ACL status tests read only the LOW WORD of the reply status
 *     longword at response+0x06 (0x00E61574 / 0x00E6159E)
 *   - the 0x3C arm's copy target, request + word(+0x8E) + 0x96 (0x00E6163E)
 *   - the bulk selection for 0x58 / 0x42 / 0x3E / everything else
 */

#include <stdio.h>
#include <string.h>

#include "rem_file/rem_file_internal.h"
#include "arch/arch.h"

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

#include "../rn_do_op.c"

/* ============================================================================
 * Globals and mocks
 * ============================================================================ */

uint16_t REM_FILE_$MAX_PROJ_LIST = 8;       /* 0xE61718 */

static status_$t mock_re_sids_status;
static status_$t mock_proj_status;
static boolean   mock_in_subsys = 0;

static void *mock_re_sids_arg1;
static void *mock_re_sids_arg2;
static void *mock_re_sids_arg3;
static void *mock_re_sids_arg4;
static void *mock_proj_arg1;
static int16_t *mock_proj_max;
static void *mock_proj_count;

/* what the last REM_FILE_$SEND_REQUEST call was handed */
static void     *snd_addr_info;
static void     *snd_request;
static int16_t   snd_request_len;
static void     *snd_extra_data;
static int16_t   snd_extra_len;
static void     *snd_response;
static uint16_t  snd_response_max;
static void     *snd_bulk_data;
static int16_t   snd_bulk_max;
static status_$t snd_status_out;
static int       snd_calls;

void ACL_$GET_RE_ALL_SIDS(void *a1, void *a2, void *a3, void *a4,
                          status_$t *status_ret)
{
    mock_re_sids_arg1 = a1;
    mock_re_sids_arg2 = a2;
    mock_re_sids_arg3 = a3;
    mock_re_sids_arg4 = a4;
    *status_ret = mock_re_sids_status;
}

void ACL_$GET_PROJ_LIST(uid_t *proj_acls, int16_t *max_count,
                        int16_t *count_ret, status_$t *status_ret)
{
    mock_proj_arg1  = proj_acls;
    mock_proj_max   = max_count;
    mock_proj_count = count_ret;
    *status_ret = mock_proj_status;
}

boolean ACL_$IN_SUBSYS(void) { return mock_in_subsys; }

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    if (len != 0) {
        memmove(dst, src, len);
    }
}

void REM_FILE_$SEND_REQUEST(void *addr_info, void *request, int16_t request_len,
                            void *extra_data, int16_t extra_len,
                            void *response, uint16_t response_max,
                            uint16_t *received_len, void *bulk_data,
                            int16_t bulk_max, int16_t *bulk_len,
                            uint16_t *packet_id, status_$t *status_ret)
{
    snd_calls++;
    snd_addr_info    = addr_info;
    snd_request      = request;
    snd_request_len  = request_len;
    snd_extra_data   = extra_data;
    snd_extra_len    = extra_len;
    snd_response     = response;
    snd_response_max = response_max;
    snd_bulk_data    = bulk_data;
    snd_bulk_max     = bulk_max;

    *received_len = 0x40;
    *bulk_len     = 0;
    *packet_id    = 0x1234;
    *status_ret   = snd_status_out;
}

/* ============================================================================
 * Fixtures
 * ============================================================================ */

static struct {
    rem_file_$rn_op_buf_t  op_buf;
    uint8_t                payload[0x200];
} va_arena;

static rem_file_$rn_op_resp_t response;
static uint32_t addr_info[2];
static uint16_t received_len;

static void reset(void)
{
    memset(&va_arena, 0, sizeof(va_arena));
    memset(&response, 0, sizeof(response));
    memset(&addr_info, 0, sizeof(addr_info));
    received_len = 0;

    mock_re_sids_status = status_$ok;
    mock_proj_status    = status_$ok;
    mock_in_subsys      = 0;
    snd_status_out      = 0x00ABCDEF;
    snd_calls           = 0;

    memset(va_arena.payload, 0x5A, sizeof(va_arena.payload));
}

static void call(int16_t base_len)
{
    REM_FILE_$RN_DO_OP(addr_info, &va_arena.op_buf, base_len, 0xBE,
                       &response, &received_len);
}

/* ============================================================================
 * The ACL prologue
 * ============================================================================ */

TEST(acl_helpers_are_handed_pointers_into_the_request)
{
    reset();
    call(0x20);

    ASSERT_EQ((unsigned long)(size_t)va_arena.op_buf.sids,
              (unsigned long)(size_t)mock_re_sids_arg2);
    ASSERT_EQ((unsigned long)(size_t)va_arena.op_buf.re_sids,
              (unsigned long)(size_t)mock_re_sids_arg4);
    ASSERT_EQ((unsigned long)(size_t)va_arena.op_buf.proj_list,
              (unsigned long)(size_t)mock_proj_arg1);
    ASSERT_EQ((unsigned long)(size_t)&va_arena.op_buf.proj_count,
              (unsigned long)(size_t)mock_proj_count);
    /* the "max" argument is the constant cell at 0xE61718, not a UID */
    ASSERT_EQ((unsigned long)(size_t)&REM_FILE_$MAX_PROJ_LIST,
              (unsigned long)(size_t)mock_proj_max);
}

TEST(a_nonzero_status_low_word_stops_before_the_send)
{
    /* ACL_$GET_RE_ALL_SIDS writes the reply's status cell, and only its LOW
     * WORD is tested ("tst.w (0x6,A0)"), so a status whose low word is zero
     * must NOT stop the send. */
    reset();
    mock_re_sids_status = 0x00010000;
    call(0x20);
    ASSERT_EQ(1, snd_calls);

    reset();
    mock_re_sids_status = 0x00000001;
    call(0x20);
    ASSERT_EQ(0, snd_calls);
}

TEST(a_nonzero_proj_list_status_low_word_stops_before_the_send)
{
    reset();
    /* ACL_$GET_PROJ_LIST writes through the same status cell */
    mock_proj_status = 0x000E0001;
    call(0x20);
    ASSERT_EQ(0, snd_calls);
}

TEST(in_subsys_sets_bit_2_of_record_byte_0x21)
{
    reset();
    mock_in_subsys = (boolean)0xFF;     /* the BYTE the image returns */
    call(0x20);
    ASSERT_EQ(0x04, va_arena.op_buf.re_sids[0x0D]);

    reset();
    mock_in_subsys = 0;
    call(0x20);
    ASSERT_EQ(0x00, va_arena.op_buf.re_sids[0x0D]);
}

TEST(magic_byte_is_stamped)
{
    reset();
    call(0x20);
    ASSERT_EQ(REM_FILE_REQ_MAGIC, va_arena.op_buf.magic);
}

/* ============================================================================
 * The 0x58 arm
 * ============================================================================ */

TEST(list_arm_short_payload_is_copied_inline)
{
    reset();
    va_arena.op_buf.op_code = REM_FILE_RN_OP_DIR_LIST;
    va_arena.op_buf.tail.list.data_va  = ARCH_PTR_TO_VA(va_arena.payload);
    va_arena.op_buf.tail.list.data_len = 0x10;
    va_arena.op_buf.tail.list.reply_va = ARCH_PTR_TO_VA(va_arena.payload);
    call(0x20);

    /* 0x20 + 0x10 <= 0x122, so it rides inline */
    ASSERT_EQ(0x30, snd_request_len);
    ASSERT_EQ(0, snd_extra_len);
    ASSERT_EQ(0x5A, va_arena.op_buf.tail.list.inline_data[0]);
    ASSERT_EQ(0x5A, va_arena.op_buf.tail.list.inline_data[0x0F]);
    ASSERT_EQ(0x00, va_arena.op_buf.tail.list.inline_data[0x10]);
}

TEST(list_arm_long_payload_travels_as_packet_data)
{
    reset();
    va_arena.op_buf.op_code = REM_FILE_RN_OP_DIR_LIST;
    va_arena.op_buf.tail.list.data_va  = ARCH_PTR_TO_VA(va_arena.payload);
    va_arena.op_buf.tail.list.data_len = 0x120;
    call(0x20);

    /* 0x20 + 0x120 > 0x122 */
    ASSERT_EQ(0x20, snd_request_len);           /* unchanged */
    ASSERT_EQ(0x120, snd_extra_len);            /* the WORD at +0x92 */
    ASSERT_EQ((unsigned long)(size_t)va_arena.payload,
              (unsigned long)(size_t)snd_extra_data);   /* the LONG at +0x8E */
    ASSERT_EQ(0x00, va_arena.op_buf.tail.list.inline_data[0]);  /* no copy */
}

TEST(list_arm_bulk_reply_is_the_long_at_0xAC_capped_at_0x400)
{
    reset();
    va_arena.op_buf.op_code = REM_FILE_RN_OP_DIR_LIST;
    va_arena.op_buf.tail.list.data_va  = ARCH_PTR_TO_VA(va_arena.payload);
    va_arena.op_buf.tail.list.data_len = 0x10;
    va_arena.op_buf.tail.list.reply_va = ARCH_PTR_TO_VA(va_arena.payload);
    call(0x20);

    ASSERT_EQ(0x400, snd_bulk_max);
    ASSERT_EQ((unsigned long)(size_t)va_arena.payload,
              (unsigned long)(size_t)snd_bulk_data);
}

/* ============================================================================
 * The 0x3C / 0x42 / 0x3E arms
 * ============================================================================ */

TEST(get_entry_arm_copies_to_record_plus_dest_off_plus_0x96)
{
    reset();
    va_arena.op_buf.op_code = REM_FILE_RN_OP_DIR_GET_ENTRY;
    va_arena.op_buf.tail.entry.dest_off = 0x10;
    va_arena.op_buf.tail.entry.data_len = 0x08;
    va_arena.op_buf.tail.entry.data_va  = ARCH_PTR_TO_VA(va_arena.payload);
    call(0x20);

    ASSERT_EQ(0x28, snd_request_len);           /* 0x20 + 8 */
    ASSERT_EQ(0, snd_extra_len);
    /* the copy target is op_buf + 0x10 + 0x96 = op_buf + 0xA6 */
    ASSERT_EQ(0x5A, ((uint8_t *)&va_arena.op_buf)[0xA6]);
    ASSERT_EQ(0x5A, ((uint8_t *)&va_arena.op_buf)[0xAD]);
    ASSERT_EQ(0x00, ((uint8_t *)&va_arena.op_buf)[0xAE]);
    /* and A2 is left holding op_buf + dest_off */
    ASSERT_EQ((unsigned long)(size_t)((uint8_t *)&va_arena.op_buf + 0x10),
              (unsigned long)(size_t)snd_extra_data);
}

TEST(get_entry_arm_long_payload_travels_as_packet_data)
{
    reset();
    va_arena.op_buf.op_code = REM_FILE_RN_OP_DIR_GET_ENTRY;
    va_arena.op_buf.tail.entry.dest_off = 0;
    va_arena.op_buf.tail.entry.data_len = 0x100;
    va_arena.op_buf.tail.entry.data_va  = ARCH_PTR_TO_VA(va_arena.payload);
    call(0x20);

    /* 0x20 + 0x100 > 0x108 */
    ASSERT_EQ(0x20, snd_request_len);
    ASSERT_EQ(0x100, snd_extra_len);
    ASSERT_EQ((unsigned long)(size_t)va_arena.payload,
              (unsigned long)(size_t)snd_extra_data);
}

TEST(read_dir_arm_clamps_the_reply_size_to_0x400)
{
    reset();
    va_arena.op_buf.op_code = REM_FILE_RN_OP_DIR_READ_DIR;
    va_arena.op_buf.tail.read_dir.reply_max = 0x1000;
    va_arena.op_buf.tail.read_dir.reply_va  = ARCH_PTR_TO_VA(va_arena.payload);
    call(0x20);

    ASSERT_EQ(0x400, snd_bulk_max);

    reset();
    va_arena.op_buf.op_code = REM_FILE_RN_OP_DIR_READ_DIR;
    va_arena.op_buf.tail.read_dir.reply_max = 0x40;
    va_arena.op_buf.tail.read_dir.reply_va  = ARCH_PTR_TO_VA(va_arena.payload);
    call(0x20);

    ASSERT_EQ(0x40, snd_bulk_max);
}

TEST(read_link_arm_uses_the_entry_view)
{
    reset();
    va_arena.op_buf.op_code = REM_FILE_RN_OP_DIR_READ_LINK;
    va_arena.op_buf.tail.entry.data_len = 0x30;
    va_arena.op_buf.tail.entry.data_va  = ARCH_PTR_TO_VA(va_arena.payload);
    call(0x20);

    ASSERT_EQ(0x30, snd_bulk_max);
    ASSERT_EQ((unsigned long)(size_t)va_arena.payload,
              (unsigned long)(size_t)snd_bulk_data);
}

TEST(default_arm_sends_no_extra_and_points_bulk_at_the_reply)
{
    reset();
    va_arena.op_buf.op_code = 0x02;             /* not one of the four */
    call(0x20);

    ASSERT_EQ(0x20, snd_request_len);
    ASSERT_EQ(0, snd_extra_len);
    ASSERT_EQ((unsigned long)(size_t)&va_arena.op_buf,
              (unsigned long)(size_t)snd_extra_data);
    ASSERT_EQ(0, snd_bulk_max);
    ASSERT_EQ((unsigned long)(size_t)&response,
              (unsigned long)(size_t)snd_bulk_data);
}

TEST(transport_status_is_copied_into_the_reply)
{
    reset();
    va_arena.op_buf.op_code = 0x02;
    snd_status_out = 0x000F0004;
    call(0x20);

    ASSERT_EQ(0x000F0004, response.status);     /* 0x00E61708 */
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)&va_arena - 0x10;

    printf("REM_FILE_$RN_DO_OP tests\n");

    RUN_TEST(acl_helpers_are_handed_pointers_into_the_request);
    RUN_TEST(a_nonzero_status_low_word_stops_before_the_send);
    RUN_TEST(a_nonzero_proj_list_status_low_word_stops_before_the_send);
    RUN_TEST(in_subsys_sets_bit_2_of_record_byte_0x21);
    RUN_TEST(magic_byte_is_stamped);
    RUN_TEST(list_arm_short_payload_is_copied_inline);
    RUN_TEST(list_arm_long_payload_travels_as_packet_data);
    RUN_TEST(list_arm_bulk_reply_is_the_long_at_0xAC_capped_at_0x400);
    RUN_TEST(get_entry_arm_copies_to_record_plus_dest_off_plus_0x96);
    RUN_TEST(get_entry_arm_long_payload_travels_as_packet_data);
    RUN_TEST(read_dir_arm_clamps_the_reply_size_to_0x400);
    RUN_TEST(read_link_arm_uses_the_entry_view);
    RUN_TEST(default_arm_sends_no_extra_and_points_bulk_at_the_reply);
    RUN_TEST(transport_status_is_copied_into_the_reply);

    printf("\n%d tests, %d failures\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
