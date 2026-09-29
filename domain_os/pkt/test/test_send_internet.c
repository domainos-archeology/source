/*
 * pkt/test/test_send_internet.c - Unit tests for PKT_$SEND_INTERNET
 * (0x00E1264E).
 *
 * The test compiles the real pkt/send_internet.c and supplies scripted
 * versions of every routine it calls, so each basic block of the original can
 * be driven from C: the header-length rejection, the optional data copy, the
 * retry-limit selection, the two out-parameters handed to
 * PKT_$BLD_INTERNET_HDR, the 0/0x2000 "do not retry" test, the retry delay and
 * the header accounting on every exit.
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

/* ==========================================================================
 * Globals and mocks the code under test links against
 * ========================================================================== */

#include "pkt/pkt_internal.h"

MODULE_DATA_DEFINE(pkt_$data_t, PKT_$DATA, 0x00E24C9C);
int8_t NETWORK_$LOOPBACK_FLAG;

/* The fake header buffer NETWORK_$GETHDR hands out. */
#define TEST_HDR_VA 0x00123400u
#define TEST_HDR_PA 0x00456000u

static int      gethdr_calls;
static uint32_t gethdr_node_seen;

static int      rtnhdr_calls;
static uint32_t rtnhdr_va_seen[8];

static int      copy_to_pa_calls;
static uint16_t copy_to_pa_len;
static void    *copy_to_pa_src;
static uint32_t *copy_to_pa_arr;
static status_$t copy_to_pa_status;   /* what the mock stores through status_ret */

static int      dump_data_calls;
static int16_t  dump_data_len;
static uint32_t *dump_data_arr;

/* Scripted PKT_$BLD_INTERNET_HDR */
static int       bld_calls;
static status_$t bld_status;          /* stored into *status_ret */
static uint16_t  bld_retry_hint;      /* stored into *retry_hint (5 in the ROM) */
static uint16_t  bld_timeout;         /* stored into *timeout_out (4 in the ROM) */
static uint16_t  bld_total_len;       /* stored into *len_out */
static int16_t   bld_port;            /* stored into *port_out */
static uint32_t  bld_hdr_buf_seen;    /* the header VA it was passed, by value */
static uint16_t *bld_retry_hint_seen;
static uint16_t *bld_timeout_seen;
static uint16_t  bld_data_len_seen;

/* Scripted NET_IO_$SEND */
static int       send_calls;
static status_$t send_status[8];      /* per-attempt status */
static uint16_t  send_info_net[8];    /* per-attempt send_info.port_net */
static uint16_t  send_info_xmit[8];   /* per-attempt send_info.xmit_status */
static int16_t   send_port_seen;
static uint32_t  send_hdr_va_seen;
static uint32_t  send_hdr_pa_seen;
static uint16_t  send_hdr_len_seen;
static uint16_t  send_flags_seen;
static int16_t   send_data_len_seen;
static uint32_t *send_data_pages_seen;

/* Scripted TIME_$WAIT */
static int       wait_calls;
static uint16_t  wait_type_seen;
static clock_t   wait_delay_seen;
static status_$t wait_status;

void PKT_$COPY_TO_PA(char *src_va, uint16_t len, uint32_t *buffers_out,
                     status_$t *status_ret)
{
    copy_to_pa_calls++;
    copy_to_pa_src = src_va;
    copy_to_pa_len = len;
    copy_to_pa_arr = buffers_out;
    *status_ret = copy_to_pa_status;
}

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    dump_data_calls++;
    dump_data_arr = buffers;
    dump_data_len = len;
}

void NETWORK_$GETHDR(uint32_t *node_ptr, uint32_t *va_out, uint32_t *ppn_out)
{
    gethdr_calls++;
    gethdr_node_seen = *node_ptr;
    *va_out = TEST_HDR_VA;
    *ppn_out = TEST_HDR_PA;
}

void NETWORK_$RTNHDR(uint32_t *va_ptr)
{
    if (rtnhdr_calls < (int)(sizeof(rtnhdr_va_seen) / sizeof(rtnhdr_va_seen[0]))) {
        rtnhdr_va_seen[rtnhdr_calls] = *va_ptr;
    }
    rtnhdr_calls++;
    *va_ptr = 0;    /* the real one consumes the slot; the caller re-primes it */
}

void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           const pkt_$info_t *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len, uint16_t data_len,
                           int16_t *port_out, pkt_$hdr_t *hdr, uint16_t *len_out,
                           uint16_t *retry_hint, uint16_t *timeout_out,
                           status_$t *status_ret)
{
    (void)routing_key; (void)dest_node; (void)dest_sock;
    (void)src_node_or; (void)src_node; (void)src_sock;
    (void)pkt_info; (void)request_id; (void)template; (void)template_len;

    bld_calls++;
    bld_hdr_buf_seen  = ARCH_PTR_TO_VA(hdr);
    bld_retry_hint_seen = retry_hint;
    bld_timeout_seen  = timeout_out;
    bld_data_len_seen = data_len;

    *port_out    = bld_port;
    *len_out     = bld_total_len;
    *retry_hint  = bld_retry_hint;
    *timeout_out = bld_timeout;
    *status_ret  = bld_status;
}

void NET_IO_$SEND(int16_t port, uint32_t *hdr_ptr, uint32_t hdr_pa,
                  uint16_t hdr_len, uint32_t data_va, uint32_t *data_pages,
                  int16_t data_len, uint16_t flags,
                  net_io_$send_info_t *send_info, status_$t *status_ret)
{
    int n = send_calls;

    (void)data_va;

    send_port_seen       = port;
    send_hdr_va_seen     = *hdr_ptr;
    send_hdr_pa_seen     = hdr_pa;
    send_hdr_len_seen    = hdr_len;
    send_flags_seen      = flags;
    send_data_len_seen   = data_len;
    send_data_pages_seen = data_pages;

    if (n >= 8) {
        n = 7;
    }
    send_info->port_net    = send_info_net[n];
    send_info->xmit_status = send_info_xmit[n];
    *status_ret            = send_status[n];

    *hdr_ptr = 0;   /* the real one writes through the slot */
    send_calls++;
}

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    wait_calls++;
    wait_type_seen  = *delay_type;
    wait_delay_seen = *delay;
    *status = wait_status;
}

#include "../send_internet.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static uint16_t retry_hint_out;
static uint16_t timeout_out;
static status_$t call_status;

static void reset_state(void)
{
    memset(&PKT_$DATA, 0, sizeof(PKT_$DATA));

    gethdr_calls = 0;
    gethdr_node_seen = 0;
    rtnhdr_calls = 0;
    memset(rtnhdr_va_seen, 0, sizeof(rtnhdr_va_seen));
    copy_to_pa_calls = 0;
    copy_to_pa_status = status_$ok;
    dump_data_calls = 0;
    dump_data_len = -1;
    dump_data_arr = NULL;

    bld_calls = 0;
    bld_status = status_$ok;
    bld_retry_hint = 5;         /* 0x00E1230E "move.w #0x5,(A0)" */
    bld_timeout = 4;            /* 0x00E1231A "move.w #0x4,(A1)" */
    bld_total_len = 0x40;
    bld_port = 3;
    bld_hdr_buf_seen = 0;
    bld_retry_hint_seen = NULL;
    bld_timeout_seen = NULL;

    send_calls = 0;
    memset(send_status, 0, sizeof(send_status));
    memset(send_info_net, 0, sizeof(send_info_net));
    memset(send_info_xmit, 0, sizeof(send_info_xmit));

    wait_calls = 0;
    wait_status = status_$ok;
    memset(&wait_delay_seen, 0, sizeof(wait_delay_seen));

    retry_hint_out = 0xEEEE;
    timeout_out = 0xEEEE;
    call_status = 0x5A5A5A5A;
}

/* One canned call; pkt_info is PKT_$DATA.ping_template unless overridden. */
static void call_send(uint16_t template_len, int16_t data_len, void *data)
{
    PKT_$SEND_INTERNET(0x11223344u, 0x00055555u, 0x0D,
                       -1, 0x00066666u, 0x0E,
                       &PKT_$DATA.ping_template, 0x1234,
                       &PKT_$DATA.ping_req_hdr, template_len,
                       data, data_len,
                       &retry_hint_out, &timeout_out,
                       &call_status);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E12674 "cmpi.w #0x200,D5w / bls" - D5 is template_len, NOT the id. */
TEST(oversize_template_rejected)
{
    reset_state();
    call_send(0x201, 0, NULL);

    ASSERT_EQ(status_$network_msg_header_too_big, call_status);
    ASSERT_EQ(0, copy_to_pa_calls);
    ASSERT_EQ(0, gethdr_calls);
    ASSERT_EQ(0, bld_calls);
    ASSERT_EQ(0, dump_data_calls);

    /* 0x200 itself passes the test. */
    reset_state();
    call_send(0x200, 0, NULL);
    ASSERT_EQ(status_$ok, call_status);
    ASSERT_EQ(1, bld_calls);
}

/*
 * 0x00E12686 "tst.w D6w / ble" and 0x00E1268A-0x00E1269C: the copy happens
 * only for a positive length, is handed the CALLER's status_$t, and the
 * original does not test that status afterwards.
 */
TEST(data_copy_only_when_positive_and_status_ignored)
{
    char payload[4] = { 1, 2, 3, 4 };

    reset_state();
    call_send(2, 0, payload);
    ASSERT_EQ(0, copy_to_pa_calls);

    reset_state();
    call_send(2, -1, payload);
    ASSERT_EQ(0, copy_to_pa_calls);

    reset_state();
    call_send(2, 8, payload);
    ASSERT_EQ(1, copy_to_pa_calls);
    ASSERT_EQ(8, copy_to_pa_len);
    ASSERT_EQ((uintptr_t)payload, (uintptr_t)copy_to_pa_src);
    ASSERT_EQ(8, bld_data_len_seen);
    ASSERT_EQ(8, send_data_len_seen);
    ASSERT_EQ(8, dump_data_len);

    /*
     * A failing copy must NOT abort the send: the original falls straight
     * through to 0x00E126A0 and the final "*status_ret = status" overwrites
     * whatever PKT_$COPY_TO_PA left behind.
     */
    reset_state();
    copy_to_pa_status = 0x00110002;
    call_send(2, 8, payload);
    ASSERT_EQ(1, bld_calls);
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(status_$ok, call_status);
}

/*
 * 0x00E126D8 "move.l (0x32,A6),-(SP)" and 0x00E126DC "pea (A3)": the builder
 * gets this function's own retry_hint and timeout_out arguments, in that
 * order, and the header buffer by value (0x00E126E2 "pea (A2)").
 */
TEST(builder_gets_the_callers_out_params)
{
    reset_state();
    call_send(2, 0, NULL);

    ASSERT_EQ(1, bld_calls);
    ASSERT_EQ((uintptr_t)&retry_hint_out, (uintptr_t)bld_retry_hint_seen);
    ASSERT_EQ((uintptr_t)&timeout_out, (uintptr_t)bld_timeout_seen);
    ASSERT_EQ(TEST_HDR_VA, bld_hdr_buf_seen);

    /* Both are visible to the caller afterwards. */
    ASSERT_EQ(5, retry_hint_out);
    ASSERT_EQ(4, timeout_out);
}

/*
 * 0x00E1272A - 0x00E1274A: NET_IO_$SEND gets the port and the total length the
 * builder produced, the header VA by reference, the physical address from
 * NETWORK_$GETHDR, the data-page vector and PKT_$DATA.default_flags.
 */
TEST(net_io_send_argument_build)
{
    reset_state();
    PKT_$DATA.default_flags = 0x00C3;
    bld_port = 7;
    bld_total_len = 0x0123;

    call_send(2, 0, NULL);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(7, send_port_seen);
    ASSERT_EQ(TEST_HDR_VA, send_hdr_va_seen);
    ASSERT_EQ(TEST_HDR_PA, send_hdr_pa_seen);
    ASSERT_EQ(0x0123, send_hdr_len_seen);
    ASSERT_EQ(0x00C3, send_flags_seen);
    ASSERT_EQ((uintptr_t)send_data_pages_seen, (uintptr_t)dump_data_arr);

    /* 0x00E126BE "pea (0xc,A6)" - the destination node goes by reference. */
    ASSERT_EQ(0x00055555u, gethdr_node_seen);
}

/*
 * 0x00E12758 "beq -> 0x00E127B6": a successful send leaves D2 holding the
 * header, so the common exit still returns it.  Reproduced deliberately.
 */
TEST(successful_send_still_returns_the_header)
{
    reset_state();
    call_send(2, 0, NULL);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(status_$ok, call_status);
    ASSERT_EQ(1, rtnhdr_calls);
    ASSERT_EQ(TEST_HDR_VA, rtnhdr_va_seen[0]);
    ASSERT_EQ(1, dump_data_calls);
}

/* 0x00E12716 "tst.l (-0x28,A6) / bne": a builder error ends the loop at once. */
TEST(builder_error_aborts)
{
    reset_state();
    bld_status = 0x0011001C;

    call_send(2, 0, NULL);

    ASSERT_EQ(1, bld_calls);
    ASSERT_EQ(0, send_calls);
    ASSERT_EQ(1, rtnhdr_calls);
    ASSERT_EQ(0x0011001C, call_status);
    ASSERT_EQ(1, dump_data_calls);
}

/*
 * 0x00E12770 "tst.w (-0x14,A6)" / 0x00E12776 "cmpi.w #0x2000,(-0x12,A6)":
 * only the pair (0, 0x2000) suppresses the retry.
 */
TEST(no_retry_on_zero_and_2000)
{
    reset_state();
    send_status[0] = 0x00110004;
    send_info_net[0] = 0;
    send_info_xmit[0] = 0x2000;

    call_send(2, 0, NULL);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(0, wait_calls);
    /* The header was returned inside the loop, so the exit does not repeat it. */
    ASSERT_EQ(1, rtnhdr_calls);
    ASSERT_EQ(0x00110004, call_status);

    /* A non-zero first word retries even with 0x2000 in the second. */
    reset_state();
    send_status[0] = 0x00110004;
    send_info_net[0] = 1;
    send_info_xmit[0] = 0x2000;
    send_status[1] = status_$ok;
    call_send(2, 0, NULL);
    ASSERT_EQ(2, send_calls);
    ASSERT_EQ(1, wait_calls);
}

/*
 * 0x00E1277E - 0x00E1279A: the retry delay is the 48-bit value {0, 25000} and
 * the wait type is the constant zero word at 0x00E127E4.
 */
TEST(retry_delay_shape)
{
    reset_state();
    send_status[0] = 0x00110004;
    send_info_net[0] = 2;
    send_status[1] = status_$ok;

    call_send(2, 0, NULL);

    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(0, wait_type_seen);
    ASSERT_EQ(0, wait_delay_seen.high);
    ASSERT_EQ(25000, wait_delay_seen.low);
}

/*
 * 0x00E126A0 "tst.w (0x8,A4) / bne" and 0x00E1271E "cmpi.w #-0x1,D4w":
 * retry_limit 0 or 0xFFFF both mean "use the builder's hint" (5 attempts);
 * any other value is the attempt ceiling.
 */
TEST(retry_limit_selection)
{
    int i;

    /* retry_limit == 0 -> five attempts. */
    reset_state();
    PKT_$DATA.ping_template.retry_limit = 0;
    for (i = 0; i < 8; i++) {
        send_status[i] = 0x00110004;
        send_info_net[i] = 1;
    }
    call_send(2, 0, NULL);
    ASSERT_EQ(5, send_calls);
    ASSERT_EQ(5, gethdr_calls);
    ASSERT_EQ(0x00110004, call_status);

    /* retry_limit == 0xFFFF -> the same, via the -1 test. */
    reset_state();
    PKT_$DATA.ping_template.retry_limit = 0xFFFF;
    for (i = 0; i < 8; i++) {
        send_status[i] = 0x00110004;
        send_info_net[i] = 1;
    }
    call_send(2, 0, NULL);
    ASSERT_EQ(5, send_calls);

    /* An explicit ceiling of 2 wins over the hint of 5. */
    reset_state();
    PKT_$DATA.ping_template.retry_limit = 2;
    for (i = 0; i < 8; i++) {
        send_status[i] = 0x00110004;
        send_info_net[i] = 1;
    }
    call_send(2, 0, NULL);
    ASSERT_EQ(2, send_calls);

    /*
     * A ceiling of 1 runs the body exactly once (it is a do/while) - but the
     * retry delay still runs, because 0x00E1277E precedes the loop test at
     * 0x00E127B0 "cmp.w D4w,D3w".
     */
    reset_state();
    PKT_$DATA.ping_template.retry_limit = 1;
    for (i = 0; i < 8; i++) {
        send_status[i] = 0x00110004;
        send_info_net[i] = 1;
    }
    call_send(2, 0, NULL);
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(1, wait_calls);
}

/*
 * 0x00E1279E - 0x00E127AE: a quit while waiting replaces the send status and
 * stops the loop.
 */
TEST(quit_while_waiting_stops_the_loop)
{
    reset_state();
    PKT_$DATA.ping_template.retry_limit = 5;
    send_status[0] = 0x00110004;
    send_info_net[0] = 1;
    send_status[1] = 0x00110004;
    send_info_net[1] = 1;
    wait_status = 0x000D0003;

    call_send(2, 0, NULL);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(0x000D0003, call_status);
    /* D2 was cleared before the wait, so no second return of the header. */
    ASSERT_EQ(1, rtnhdr_calls);
    ASSERT_EQ(1, dump_data_calls);
}

/* 0x00E127CA - 0x00E127D2: PKT_$DUMP_DATA runs on every exit past the length
 * check, with the page vector and the data length. */
TEST(dump_data_always_runs)
{
    char payload[4] = { 9, 9, 9, 9 };

    reset_state();
    bld_status = 0x0011001C;
    call_send(2, 4, payload);
    ASSERT_EQ(1, dump_data_calls);
    ASSERT_EQ(4, dump_data_len);

    reset_state();
    call_send(2, 4, payload);
    ASSERT_EQ(1, dump_data_calls);
    ASSERT_EQ(4, dump_data_len);
}

int main(void)
{
    printf("PKT_$SEND_INTERNET tests\n");

    RUN_TEST(oversize_template_rejected);
    RUN_TEST(data_copy_only_when_positive_and_status_ignored);
    RUN_TEST(builder_gets_the_callers_out_params);
    RUN_TEST(net_io_send_argument_build);
    RUN_TEST(successful_send_still_returns_the_header);
    RUN_TEST(builder_error_aborts);
    RUN_TEST(no_retry_on_zero_and_2000);
    RUN_TEST(retry_delay_shape);
    RUN_TEST(retry_limit_selection);
    RUN_TEST(quit_while_waiting_stops_the_loop);
    RUN_TEST(dump_data_always_runs);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
