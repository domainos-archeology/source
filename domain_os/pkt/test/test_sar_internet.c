/*
 * pkt/test/test_sar_internet.c - Unit tests for PKT_$SAR_INTERNET
 * (0x00E71EC4).
 *
 * The tests compile the real pkt/sar_internet.c and script everything it
 * calls, so the three details bead source-wsxe corrected can be checked
 * against the image:
 *
 *   - the attempt counter goes to the TENTH argument's word at +0x08
 *     ("movea.l (0x24,A6),A0 / move.w D3w,(0x8,A0)" at 0x00E7205E), while the
 *     packet-info record the retry limit was read from is left alone;
 *   - the APP_$RECEIVE result is an app_$receive_rec_t: the template is
 *     copied from rec.data (+0x04, 0x00E720E6), NETBUF_$RTN_HDR is handed
 *     &rec.data unmasked (0x00E720FC), and PKT_$DAT_COPY / PKT_$DUMP_DATA are
 *     handed rec.data_pages (+0x08, 0x00E7212A / 0x00E72142);
 *   - the CRASH_SYSTEM cell at 0x00E72180 holds 0x00110005.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

/* ==========================================================================
 * Globals the image reads
 * ========================================================================== */

uint32_t NODE_$ME = 0x00012345;
uint32_t TIME_$CLOCKH = 1000;
uint8_t sock_table_base[SOCK_TABLE_SIZE];
uint32_t FIM_$QUIT_VALUE[64];
ec_$eventcount_t FIM_$QUIT_EC[64];
uint16_t PROC1_$AS_ID = 3;

static ec_$eventcount_t sock_eventcount;

/* ==========================================================================
 * Scripted callees
 * ========================================================================== */

static status_$t crash_status_seen;
static int crash_calls;

void CRASH_SYSTEM(const status_$t *status)
{
    crash_status_seen = *status;
    crash_calls++;
}

static int8_t sock_alloc_result;
static uint16_t sock_alloc_num;
static int sock_close_calls;
static uint16_t sock_close_seen;

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages,
                      uint32_t max_queue)
{
    (void)proto_bufpages; (void)max_queue;
    *sock_ret = sock_alloc_num;
    return sock_alloc_result;
}

void SOCK_$CLOSE(uint16_t sock_num)
{
    sock_close_seen = sock_num;
    sock_close_calls++;
}

static int16_t next_id_value;
int16_t PKT_$NEXT_ID(void) { return next_id_value; }

/* PKT_$SEND_INTERNET: hands back the two hints and a status. */
static uint16_t send_retry_hint;
static uint16_t send_rtt_hint;
static status_$t send_status;
static int send_calls;

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node,
                        uint16_t dest_sock, int32_t src_node_or,
                        uint32_t src_node, uint16_t src_sock, void *pkt_info,
                        uint16_t request_id, void *template,
                        uint16_t template_len, void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    (void)routing_key; (void)dest_node; (void)dest_sock; (void)src_node_or;
    (void)src_node; (void)src_sock; (void)pkt_info; (void)request_id;
    (void)template; (void)template_len; (void)data; (void)data_len;
    send_calls++;
    *retry_hint = send_retry_hint;
    *timeout_out = send_rtt_hint;
    *status_ret = send_status;
}

/*
 * EC_$WAIT: returns a scripted sequence of 0 (packet), 1 (timeout) and
 * 2 (quit), one entry per call.
 */
static int16_t wait_script[16];
static int wait_script_len;
static int wait_calls;

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    (void)ecs; (void)vals;
    if (wait_calls >= wait_script_len) {
        return 1;                       /* fall back to "timed out" */
    }
    return wait_script[wait_calls++];
}

/* APP_$RECEIVE: fills the caller's record from a template. */
static app_$receive_rec_t app_rec_template;
static status_$t app_receive_status;
static int app_receive_calls;

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    (void)sock_num;
    app_receive_calls++;
    *(app_$receive_rec_t *)result = app_rec_template;
    *status_ret = app_receive_status;
}

static const void *data_copy_src;
static void *data_copy_dst;
static uint32_t data_copy_len;

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    data_copy_src = src;
    data_copy_dst = dst;
    data_copy_len = len;
}

static uint32_t *rtn_hdr_arg;
static uint32_t rtn_hdr_value;
static int rtn_hdr_calls;

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    rtn_hdr_arg = va_ptr;
    rtn_hdr_value = *va_ptr;
    rtn_hdr_calls++;
}

static uint32_t *dat_copy_buffers;
static int16_t dat_copy_len;
static char *dat_copy_dest;
static int dat_copy_calls;

void PKT_$DAT_COPY(uint32_t *buffers, int16_t len, char *dest_va)
{
    dat_copy_buffers = buffers;
    dat_copy_len = len;
    dat_copy_dest = dest_va;
    dat_copy_calls++;
}

static uint32_t *dump_data_buffers;
static int16_t dump_data_len;
static int dump_data_calls;

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    dump_data_buffers = buffers;
    dump_data_len = len;
    dump_data_calls++;
}

static uint32_t note_visible_node;
static boolean note_visible_flag;
static int note_visible_calls;

void PKT_$NOTE_VISIBLE(uint32_t node_id, boolean is_visible)
{
    note_visible_node = node_id;
    note_visible_flag = is_visible;
    note_visible_calls++;
}

static boolean likely_result;
static pkt_$net_addr_t likely_seen_addr;
static int likely_calls;

boolean PKT_$LIKELY_TO_ANSWER(void *addr_info, status_$t *status_ret)
{
    likely_seen_addr = *(pkt_$net_addr_t *)addr_info;
    likely_calls++;
    (void)status_ret;
    return likely_result;
}

/* ==========================================================================
 * The function under test
 * ========================================================================== */

#include "../sar_internet.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

static pkt_$info_t info;
static pkt_$sar_result_t resp;
static char tpl_buf[64];
static char data_buf[64];
static uint16_t tpl_len_out;
static uint16_t data_len_out;
static status_$t st;

/*
 * The record's +0x00 and +0x04 fields are 32-bit TARGET virtual addresses, so
 * the test lays both objects out in an arena and points ARCH_HOST_VA_BASE at
 * it (main() below).
 */
static uint8_t va_arena[0x400];
#define reply_hdr (*(app_$reply_hdr_t *)(va_arena + 0x100))
#define payload   ((char *)(va_arena + 0x200))

static void reset(void)
{
    memset(&info, 0, sizeof(info));
    memset(&resp, 0xAA, sizeof(resp));
    memset(tpl_buf, 0, sizeof(tpl_buf));
    memset(data_buf, 0, sizeof(data_buf));
    memset(&app_rec_template, 0, sizeof(app_rec_template));
    memset(va_arena, 0, sizeof(va_arena));
    memset(FIM_$QUIT_VALUE, 0, sizeof(FIM_$QUIT_VALUE));
    memset(FIM_$QUIT_EC, 0, sizeof(FIM_$QUIT_EC));

    sock_eventcount.value = 7;
    SOCK_$EVENT_COUNTERS[0] = &sock_eventcount;   /* slot for socket 1 */

    sock_alloc_result = -1;         /* bmi taken: allocation succeeded */
    sock_alloc_num = 1;
    sock_close_calls = 0;
    crash_calls = 0;
    crash_status_seen = 0;
    next_id_value = 0x1234;
    send_retry_hint = 3;
    send_rtt_hint = 5;
    send_status = status_$ok;
    send_calls = 0;
    wait_script_len = 0;
    wait_calls = 0;
    app_receive_status = status_$ok;
    app_receive_calls = 0;
    data_copy_src = NULL;
    data_copy_dst = NULL;
    data_copy_len = 0;
    rtn_hdr_arg = NULL;
    rtn_hdr_value = 0;
    rtn_hdr_calls = 0;
    dat_copy_calls = 0;
    dump_data_calls = 0;
    note_visible_calls = 0;
    likely_calls = 0;
    likely_result = 0;
    tpl_len_out = 0xFFFF;
    data_len_out = 0xFFFF;
    st = 0x7F7F7F7F;

    reply_hdr.magic = 0x0118;
    reply_hdr.template_len = 8;
    reply_hdr.data_len = 12;
    reply_hdr.request_id = next_id_value;

    app_rec_template.reply = ARCH_PTR_TO_VA(&reply_hdr);
    app_rec_template.data = ARCH_PTR_TO_VA(payload);
}

static void call(void)
{
    PKT_$SAR_INTERNET(0x11112222, 0x00033333, 4, &info, 6,
                      tpl_buf, 8, data_buf, 0,
                      &resp, tpl_buf, (uint16_t)sizeof(tpl_buf),
                      &tpl_len_out, data_buf, (uint16_t)sizeof(data_buf),
                      &data_len_out, &st);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * The socket-allocation crash cell.  SOCK_$ALLOCATE returning a non-negative
 * byte takes the "bpl" fall-through at 0x00E71EF4 into the CRASH_SYSTEM call.
 */
TEST(socket_allocation_failure_crashes_with_0x110005)
{
    reset();
    sock_alloc_result = 0;
    wait_script[0] = 1;                 /* one timeout, then give up */
    wait_script_len = 1;
    info.retry_limit = 1;               /* max_retries == 1 == retry_num */
    call();
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00110005, crash_status_seen);
}

/*
 * The no-answer exit stores the attempt count in the TENTH argument at +0x08
 * (0x00E7205E) and leaves the packet-info record's own retry limit alone.
 */
TEST(no_answer_stores_attempts_in_tenth_argument)
{
    reset();
    info.retry_limit = 3;
    wait_script[0] = 1;
    wait_script[1] = 1;
    wait_script[2] = 1;
    wait_script_len = 3;
    likely_result = -1;                 /* bmi: "likely", so keep retrying */
    call();

    ASSERT_EQ(3, resp.attempts);
    ASSERT_EQ(3, info.retry_limit);     /* untouched */
    ASSERT_EQ(status_$network_remote_node_failed_to_respond, st);
    ASSERT_EQ(3, send_calls);
    ASSERT_EQ(1, sock_close_calls);
    /* retry_num > 2 on the last attempt, so visibility is cleared once */
    ASSERT_EQ(1, note_visible_calls);
    ASSERT_EQ(0, note_visible_flag);
}

/*
 * A zero retry limit means "take the hint PKT_$SEND_INTERNET returns"
 * (0x00E71F52 / 0x00E71FBE); the count still lands in the tenth argument.
 */
TEST(zero_retry_limit_uses_send_hint)
{
    reset();
    info.retry_limit = 0;
    send_retry_hint = 3;                /* adopted as max_retries */
    wait_script[0] = 1;
    wait_script[1] = 1;
    wait_script_len = 2;
    likely_result = 0;                  /* bpl at 0x00E7205C: give up */
    call();

    ASSERT_EQ(2, resp.attempts);
    ASSERT_EQ(0, info.retry_limit);
    ASSERT_EQ(1, likely_calls);
    ASSERT_EQ(0x11112222, likely_seen_addr.network);
    ASSERT_EQ(0x00033333, likely_seen_addr.node);
}

/*
 * The successful path reads the record, not the reply header, for the data
 * pointer and the page vector.
 */
TEST(receive_record_fields_drive_the_copies)
{
    reset();
    info.retry_limit = 4;
    wait_script[0] = 0;                 /* packet available */
    wait_script_len = 1;
    app_rec_template.data_pages[0] = 0x00090000;
    app_rec_template.data_pages[1] = 0x000A0000;
    call();

    /* template copied from rec.data (+0x04), not from rec.reply */
    ASSERT_EQ((long long)(uintptr_t)payload, (long long)(uintptr_t)data_copy_src);
    ASSERT_EQ((long long)(uintptr_t)tpl_buf, (long long)(uintptr_t)data_copy_dst);
    ASSERT_EQ(8, data_copy_len);
    ASSERT_EQ(8, tpl_len_out);

    /* NETBUF_$RTN_HDR gets &rec.data, unmasked */
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(payload), rtn_hdr_value);

    /* the page vector is rec.data_pages, i.e. rec + 8 */
    ASSERT_EQ(1, dat_copy_calls);
    ASSERT_EQ(12, dat_copy_len);
    ASSERT_EQ(0x00090000, dat_copy_buffers[0]);
    ASSERT_EQ(0x000A0000, dat_copy_buffers[1]);
    ASSERT_EQ(1, dump_data_calls);
    ASSERT_EQ(12, dump_data_len);
    ASSERT_EQ((long long)(uintptr_t)dat_copy_buffers,
              (long long)(uintptr_t)dump_data_buffers);
    ASSERT_EQ(12, data_len_out);

    /* matching id, clean status: visibility is set true and the socket closed */
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, note_visible_calls);
    ASSERT_EQ((int8_t)0xFF, note_visible_flag);
    ASSERT_EQ(1, sock_close_calls);
    /* the tenth argument is untouched on the success path */
    ASSERT_EQ(0xAAAA, resp.attempts);
}

/* An empty page vector reports a zero data length and skips both PKT calls. */
TEST(no_data_pages_reports_zero_length)
{
    reset();
    info.retry_limit = 4;
    wait_script[0] = 0;
    wait_script_len = 1;
    app_rec_template.data_pages[0] = 0;
    call();

    ASSERT_EQ(0, data_len_out);
    ASSERT_EQ(0, dat_copy_calls);
    ASSERT_EQ(0, dump_data_calls);
}

/* Both clamps are unsigned "bls" compares against the caller's capacities. */
TEST(lengths_are_clamped_to_the_caller_maxima)
{
    reset();
    info.retry_limit = 4;
    reply_hdr.template_len = 500;
    reply_hdr.data_len = 400;
    wait_script[0] = 0;
    wait_script_len = 1;
    app_rec_template.data_pages[0] = 0x00090000;
    call();

    ASSERT_EQ(sizeof(tpl_buf), tpl_len_out);
    ASSERT_EQ(sizeof(tpl_buf), data_copy_len);
    ASSERT_EQ(sizeof(data_buf), data_len_out);
    ASSERT_EQ(sizeof(data_buf), dat_copy_len);
    /* PKT_$DUMP_DATA re-reads the reply header, so it frees the full length */
    ASSERT_EQ(400, dump_data_len);
}

/* The quit arm snapshots FIM_$QUIT_EC into FIM_$QUIT_VALUE and reports 0x120010. */
TEST(quit_arm_snapshots_the_quit_eventcount)
{
    reset();
    info.retry_limit = 4;
    FIM_$QUIT_EC[PROC1_$AS_ID].value = 0x55;
    wait_script[0] = 2;
    wait_script_len = 1;
    call();

    ASSERT_EQ(0x55, FIM_$QUIT_VALUE[PROC1_$AS_ID]);
    ASSERT_EQ(0x120010, st);
    ASSERT_EQ(0, note_visible_calls);
    ASSERT_EQ(1, sock_close_calls);
    ASSERT_EQ(0xAAAA, resp.attempts);
}

/* A failed send closes the socket and leaves everything else alone. */
TEST(send_failure_closes_and_returns)
{
    reset();
    info.retry_limit = 4;
    send_status = 0x00110001;
    call();

    ASSERT_EQ(0x00110001, st);
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(0, wait_calls);
    ASSERT_EQ(0, note_visible_calls);
    ASSERT_EQ(1, sock_close_calls);
    ASSERT_EQ(0xAAAA, resp.attempts);
}

/* A reply carrying the wrong id keeps the inner wait loop going. */
TEST(mismatched_reply_id_keeps_waiting)
{
    reset();
    info.retry_limit = 1;
    reply_hdr.request_id = next_id_value + 1;
    wait_script[0] = 0;                 /* one wrong-id packet */
    wait_script[1] = 1;                 /* then a timeout */
    wait_script_len = 2;
    call();

    ASSERT_EQ(2, wait_calls);
    ASSERT_EQ(1, app_receive_calls);
    ASSERT_EQ(1, resp.attempts);
    ASSERT_EQ(status_$network_remote_node_failed_to_respond, st);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;
    printf("PKT_$SAR_INTERNET tests\n");
    RUN_TEST(socket_allocation_failure_crashes_with_0x110005);
    RUN_TEST(no_answer_stores_attempts_in_tenth_argument);
    RUN_TEST(zero_retry_limit_uses_send_hint);
    RUN_TEST(receive_record_fields_drive_the_copies);
    RUN_TEST(no_data_pages_reports_zero_length);
    RUN_TEST(lengths_are_clamped_to_the_caller_maxima);
    RUN_TEST(quit_arm_snapshots_the_quit_eventcount);
    RUN_TEST(send_failure_closes_and_returns);
    RUN_TEST(mismatched_reply_id_keeps_waiting);
    printf("\n%d run, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
