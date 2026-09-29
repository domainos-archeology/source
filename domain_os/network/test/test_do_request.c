/*
 * network/test/test_do_request.c
 *
 * network_$do_request (0x00E0F86C) validates the reply by comparing the WORD
 * at the head of the REPLY BUFFER against the request type plus one, and then
 * takes the caller's status from the unaligned longword at REPLY BUFFER + 2:
 *
 *   0x00E0F9CC  move.w (A4),D0w      A4 = the (0x1a,A6) argument, resp_buf
 *   0x00E0F9CE  movea.l D7,A0        D7 = the (0x0c,A6) argument, cmd_buf
 *   0x00E0F9D2  move.w (A0),D1w / ext.l D1 / addq.l #0x1,D1
 *   0x00E0F9DA  bne -> status_$network_unexpected_reply_type
 *   0x00E0F9E4  move.l (0x2,A4),(A3)
 *
 * The resp_info argument (0x1e,A6) is handed to network_$wait_response and is
 * never read here.
 */

#include <stdio.h>
#include <string.h>

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
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Module data and stubs
 * ============================================================================ */

#include "network/network_internal.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "misc/misc.h"

uint32_t NETWORK_$MOTHER_NODE;
int16_t  NETWORK_$RETRY_TIMEOUT;
/* The SOCK module data block (sock/sock.h). */
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);

static int32_t  sock_ec;
static int      close_calls;
static int      crash_calls;
static int      note_visible_calls;
static int8_t   last_note_visible_flag;
static uint32_t last_note_visible_node;

/* what the stubbed helpers should do */
static status_$t stub_send_status;
static uint16_t  stub_max_retries;
static int16_t   stub_timeout;
static int8_t    stub_wait_result;
static int       wait_calls;
static uint8_t   stub_reply[0x40];      /* copied into resp_buf */
static uint8_t   stub_resp_info[0x10];  /* copied into resp_info */

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages, uint32_t max_queue)
{
    (void)proto_bufpages; (void)max_queue;
    *sock_ret = 1;
    return -1;                          /* Domain true: allocated */
}

void SOCK_$CLOSE(uint16_t sock_num) { (void)sock_num; close_calls++; }

int16_t PKT_$NEXT_ID(void) { return 0x1234; }

void PKT_$NOTE_VISIBLE(uint32_t node, int8_t flag)
{
    note_visible_calls++;
    last_note_visible_node = node;
    last_note_visible_flag = flag;
}

void PKT_$DUMP_DATA(uint32_t *bufs, int16_t len) { (void)bufs; (void)len; }

int8_t PKT_$LIKELY_TO_ANSWER(void *handle, status_$t *status_ret)
{
    (void)handle; (void)status_ret;
    return -1;                          /* keep retrying */
}

void CRASH_SYSTEM(const status_$t *status_p) { (void)status_p; crash_calls++; }

void network_$send_request(void *net_handle, int16_t sock_num, int16_t pkt_id,
                           int16_t *cmd_buf, int16_t cmd_len, int16_t param_hi,
                           uint32_t param_lo, uint16_t *retry_count_out,
                           int16_t *timeout_out, status_$t *status_ret)
{
    (void)net_handle; (void)sock_num; (void)pkt_id; (void)cmd_buf;
    (void)cmd_len; (void)param_hi; (void)param_lo;
    *retry_count_out = stub_max_retries;
    *timeout_out     = stub_timeout;
    *status_ret      = stub_send_status;
}

int8_t network_$wait_response(int16_t sock_num, int16_t pkt_id,
                              uint16_t timeout, int32_t *event_count,
                              int16_t *resp_buf, int16_t *resp_len_out,
                              uint32_t *data_bufs, uint16_t *data_len_out)
{
    (void)sock_num; (void)pkt_id; (void)timeout; (void)event_count;
    wait_calls++;
    memcpy(resp_buf, stub_reply, sizeof(stub_reply));
    memcpy(resp_len_out, stub_resp_info, sizeof(stub_resp_info));
    data_bufs[0] = 0;
    *data_len_out = 0;
    return stub_wait_result;
}

#include "../do_request.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static uint32_t net_handle[4];
static int16_t  cmd_buf[8];
static uint8_t  resp_buf[0x40];
static uint8_t  resp_info[0x10];
static status_$t status;

static void reset_all(void)
{
    memset(net_handle, 0, sizeof(net_handle));
    memset(cmd_buf, 0, sizeof(cmd_buf));
    memset(resp_buf, 0, sizeof(resp_buf));
    memset(resp_info, 0, sizeof(resp_info));
    memset(stub_reply, 0, sizeof(stub_reply));
    memset(stub_resp_info, 0, sizeof(stub_resp_info));
    sock_ec = 0;
    memset(&SOCK_$DATA, 0, sizeof(SOCK_$DATA));
    /* the mocked SOCK_$ALLOCATE hands out socket 1 (0x00E0F8BA-0x00E0F8C8) */
    SOCK_$DATA.socket_ptr[1] = (sock_$sock_t *)&sock_ec;
    NETWORK_$MOTHER_NODE = 0;
    NETWORK_$RETRY_TIMEOUT = 0;
    close_calls = crash_calls = note_visible_calls = wait_calls = 0;
    stub_send_status = status_$ok;
    stub_max_retries = 3;
    stub_timeout     = 10;
    stub_wait_result = -1;              /* a reply arrived */
    status = 0x5A5A5A5A;

    net_handle[1] = 0x0000ABCD;         /* the target node, handle + 4 */
    cmd_buf[0] = 0x0010;                /* request type */
}

static void call(void)
{
    network_$do_request(net_handle, cmd_buf, 0x2A, 0, 0, 0,
                        resp_buf, resp_info, &status);
}

/* ============================================================================
 * The reply header
 * ============================================================================ */

TEST(reply_header_layout) {
    ASSERT_EQ(0x00u, offsetof(network_$reply_hdr_t, reply_type));
    ASSERT_EQ(0x02u, offsetof(network_$reply_hdr_t, status));
    ASSERT_EQ(6u,    sizeof(network_$reply_hdr_t));
}

TEST(status_comes_from_the_reply_buffer) {
    reset_all();
    /* a well-formed reply: type = request + 1, then the reply's own status */
    *(uint16_t *)&stub_reply[0] = 0x0011;
    {
        uint32_t reply_status = 0x00CAFEBAu;
        uint32_t other_status = 0xDEADBEEFu;

        /* the longword at reply + 2 is UNALIGNED, hence the memcpy */
        memcpy(&stub_reply[2], &reply_status, sizeof(reply_status));
        /* resp_info carries something else entirely; it must NOT be read */
        memcpy(&stub_resp_info[2], &other_status, sizeof(other_status));
    }

    call();

    ASSERT_EQ(0x00CAFEBA, status);
}

TEST(resp_info_is_never_the_status_source) {
    reset_all();
    *(uint16_t *)&stub_reply[0] = 0x0011;
    {
        uint32_t reply_status = 0u;
        uint32_t other_status = 0x00110018u;

        memcpy(&stub_reply[2], &reply_status, sizeof(reply_status));
        memcpy(&stub_resp_info[2], &other_status, sizeof(other_status));
    }

    call();

    /* the reply says "ok"; the resp_info bytes would have said 0x00110018 */
    ASSERT_EQ(status_$ok, status);
}

TEST(wrong_reply_type_is_rejected) {
    reset_all();
    *(uint16_t *)&stub_reply[0] = 0x0012;       /* request + 2 */
    {
        uint32_t reply_status = 0x00CAFEBAu;

        memcpy(&stub_reply[2], &reply_status, sizeof(reply_status));
    }

    call();

    ASSERT_EQ(status_$network_unexpected_reply_type, status);
}

TEST(reply_type_compare_is_signed) {
    reset_all();
    /* both words are sign-extended before the longword compare */
    cmd_buf[0] = -2;
    *(uint16_t *)&stub_reply[0] = 0xFFFF;       /* -1 == -2 + 1 */
    {
        uint32_t reply_status = 0x00010002u;

        memcpy(&stub_reply[2], &reply_status, sizeof(reply_status));
    }

    call();

    ASSERT_EQ(0x00010002, status);
}

TEST(a_reply_marks_the_node_visible_and_closes_the_socket) {
    reset_all();
    *(uint16_t *)&stub_reply[0] = 0x0011;
    call();
    ASSERT_EQ(1, note_visible_calls);
    ASSERT_EQ(0x0000ABCDu, last_note_visible_node);
    ASSERT_EQ((int8_t)-1, last_note_visible_flag);      /* "st" = true */
    ASSERT_EQ(1, close_calls);
}

/* ============================================================================
 * The retry bound
 * ============================================================================ */

TEST(retry_count_compare_zero_extends_max_retries) {
    reset_all();
    stub_wait_result = 0;               /* every wait times out */
    stub_max_retries = 2;
    call();

    /* two sends succeed, the third pass gives up */
    ASSERT_EQ(2, wait_calls);
    ASSERT_EQ(status_$network_remote_node_failed_to_respond, status);
    ASSERT_EQ((int8_t)0, last_note_visible_flag);       /* "clr.w" = false */
}

TEST(mother_node_is_never_given_up_on) {
    reset_all();
    stub_wait_result = 0;
    stub_max_retries = 1;
    NETWORK_$MOTHER_NODE = 0x0000ABCD;  /* the target IS the mother node */
    stub_send_status = 0x00110004;      /* stop the loop the other way */
    call();

    /* the send status ends the loop before the retry bound can */
    ASSERT_EQ(0x00110004, status);
    ASSERT_EQ(0, wait_calls);
}

TEST(a_send_failure_ends_the_loop) {
    reset_all();
    stub_send_status = 0x00110001;
    call();
    ASSERT_EQ(0, wait_calls);
    ASSERT_EQ(0x00110001, status);
    ASSERT_EQ(1, close_calls);
}

int main(void)
{
    printf("network_$do_request tests\n");
    RUN_TEST(reply_header_layout);
    RUN_TEST(status_comes_from_the_reply_buffer);
    RUN_TEST(resp_info_is_never_the_status_source);
    RUN_TEST(wrong_reply_type_is_rejected);
    RUN_TEST(reply_type_compare_is_signed);
    RUN_TEST(a_reply_marks_the_node_visible_and_closes_the_socket);
    RUN_TEST(retry_count_compare_zero_extends_max_retries);
    RUN_TEST(mother_node_is_never_given_up_on);
    RUN_TEST(a_send_failure_ends_the_loop);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
