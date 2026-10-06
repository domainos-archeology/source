/*
 * network/test/test_send_request.c - network_$send_request (0x00E0F5F4) and
 * network_$wait_response (0x00E0F746)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "network/network_internal.h"
#include "net_io/net_io.h"
#include "os/os.h"
#include "netbuf/netbuf.h"
#include "time/time.h"

uint32_t NETWORK_$MOTHER_NODE;
uint32_t NODE_$ME = 0x00000123;
uint32_t TIME_$CLOCKH;
pkt_$info_t NETWORK_$REQUEST_PKT_INFO;
clock_t NETWORK_$RETRY_DELAY = { 0, 0x09C4 };
uint16_t NETWORK_$REQUEST_SEND_FLAGS = 1;
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);

/* VA arena for headers and replies */
static uint8_t arena[0x400] __attribute__((aligned(8)));

static char trace[64];
static int ntrace;

/* ---- send_request mocks ---- */
static int gethdr_calls, rtnhdr_calls;
void NETWORK_$GETHDR(uint32_t *node_ptr, uint32_t *va, uint32_t *pa)
{
    (void)node_ptr;
    gethdr_calls++;
    trace[ntrace++] = 'G';
    *va = 0x100;
    *pa = 0x7000;
}
void NETWORK_$RTNHDR(uint32_t *va) { (void)va; rtnhdr_calls++; trace[ntrace++] = 'R'; }

static status_$t bld_status;
static uint32_t bld_key, bld_node, bld_src;
static uint16_t bld_dsock, bld_ssock, bld_id, bld_tlen, bld_dlen;
static int32_t bld_or;
void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           const pkt_$info_t *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len, uint16_t data_len,
                           int16_t *port_out, pkt_$hdr_t *hdr, uint16_t *len_out,
                           uint16_t *retry_hint, uint16_t *timeout_out,
                           status_$t *status_ret)
{
    (void)pkt_info; (void)template; (void)hdr;
    trace[ntrace++] = 'B';
    bld_key = routing_key; bld_node = dest_node; bld_dsock = dest_sock;
    bld_or = src_node_or; bld_src = src_node; bld_ssock = src_sock;
    bld_id = request_id; bld_tlen = template_len; bld_dlen = data_len;
    *port_out = 3;
    *len_out = 0x40;
    *retry_hint = 2;
    *timeout_out = 7;
    *status_ret = bld_status;
}

static status_$t send_status[8];
static net_io_$send_info_t send_info[8];
static int nsend;
static uint32_t send_page;
static uint16_t send_flags;
void NET_IO_$SEND(int16_t port, uint32_t *hdr_ptr, uint32_t hdr_pa,
                  uint16_t hdr_len, uint32_t data_va, uint32_t *data_pages,
                  int16_t data_len, uint16_t flags,
                  net_io_$send_info_t *info, status_$t *status_ret)
{
    (void)port; (void)hdr_ptr; (void)hdr_pa; (void)hdr_len; (void)data_va;
    (void)data_len;
    trace[ntrace++] = 'S';
    send_page = data_pages[0];
    send_flags = flags;
    *info = send_info[nsend];
    *status_ret = send_status[nsend];
    nsend++;
}

static int nwait;
void TIME_$WAIT(uint16_t *type, clock_t *delay, status_$t *st)
{
    (void)st;
    trace[ntrace++] = 'W';
    nwait++;
    if (*type != 0 || delay->low != 0x09C4) { tests_failed++; }
}

/* ---- wait_response mocks ---- */
static int16_t wait_which[8];
static int nwaitec;
static int32_t ec_val_seen, deadline_seen;
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    (void)ecs;
    ec_val_seen = vals.val[0];
    deadline_seen = vals.val[1];
    return wait_which[nwaitec++];
}

typedef struct { status_$t st; int16_t id; uint16_t tlen; uint16_t dlen; uint32_t page0; } rcv_t;
static rcv_t rcvs[8];
static int nrcv;
void APP_$RECEIVE(uint16_t sock, void *result, status_$t *st)
{
    app_$receive_rec_t *r = (app_$receive_rec_t *)result;
    app_$reply_hdr_t *rh = (app_$reply_hdr_t *)&arena[0x200];
    (void)sock;
    memset(r, 0, sizeof(*r));
    rh->template_len = rcvs[nrcv].tlen;
    rh->data_len = rcvs[nrcv].dlen;
    rh->request_id = rcvs[nrcv].id;
    r->reply = 0x200;
    r->data = 0x1210;               /* page 0x1000 + 0x210 */
    r->data_pages[0] = rcvs[nrcv].page0;
    r->data_pages[1] = 0x55;
    *st = rcvs[nrcv].st;
    nrcv++;
}
static uint32_t copy_len;
void OS_$DATA_COPY(const void *src, void *dst, uint32_t len) { (void)src; (void)dst; copy_len = len; }
static uint32_t rtn_hdr_va;
void NETBUF_$RTN_HDR(uint32_t *va) { rtn_hdr_va = *va; }
static int ndump;
void PKT_$DUMP_DATA(uint32_t *bufs, int16_t len) { (void)bufs; (void)len; ndump++; }

#include "../send_request.c"
#include "../wait_response.c"

static uint32_t net[2] = { 0x11, 0x456 };
static uint16_t retry;
static int16_t timeout;

static void setup(void)
{
    memset(trace, 0, sizeof(trace));
    ntrace = nsend = nwait = gethdr_calls = rtnhdr_calls = 0;
    memset(send_status, 0, sizeof(send_status));
    memset(send_info, 0, sizeof(send_info));
    bld_status = 0;
    NETWORK_$MOTHER_NODE = 0x999;
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
}

TEST(send_ok_first_time)
{
    status_$t st = 1;
    int16_t cmd[4] = { 0 };
    setup();
    network_$send_request(net, 5, 0x77, cmd, 8, 0xABC00, 0x400, &retry, &timeout, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, strcmp(trace, "GBSR"));
    ASSERT_EQ(0x11, bld_key);
    ASSERT_EQ(0x456, bld_node);
    ASSERT_EQ(1, bld_dsock);
    ASSERT_EQ(0xFFFFFFFF, (uint32_t)bld_or);
    ASSERT_EQ(0x123, bld_src);
    ASSERT_EQ(5, bld_ssock);
    ASSERT_EQ(0x77, bld_id);
    ASSERT_EQ(0x400, bld_dlen);
    ASSERT_EQ(0xABC00, send_page);
    ASSERT_EQ(1, send_flags);
    ASSERT_EQ(2, retry);
    ASSERT_EQ(7, timeout);
}

TEST(build_failure_returns_header)
{
    status_$t st = 0;
    int16_t cmd[4] = { 0 };
    setup();
    bld_status = 0x110001;
    network_$send_request(net, 5, 1, cmd, 8, 0, 0, &retry, &timeout, &st);
    ASSERT_EQ(0x110001, st);
    ASSERT_EQ(0, strcmp(trace, "GBR"));
}

TEST(retry_then_success_and_benign_bits)
{
    status_$t st = 0;
    int16_t cmd[4] = { 0 };
    setup();
    send_status[0] = 0x110005;
    send_info[0].port_net = 1;          /* port_net != 0: straight to retry */
    send_status[1] = 0x110005;
    send_info[1].xmit_status = 0x0020;  /* bit 5: treated as delivered */
    network_$send_request(net, 5, 1, cmd, 8, 0, 0, &retry, &timeout, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, strcmp(trace, "GBSRWGBSR"));
}

TEST(parity_error_returns_at_once)
{
    status_$t st = 0;
    int16_t cmd[4] = { 0 };
    setup();
    send_status[0] = status_$network_memory_parity_error_during_transmit;
    network_$send_request(net, 5, 1, cmd, 8, 0, 0, &retry, &timeout, &st);
    ASSERT_EQ(status_$network_memory_parity_error_during_transmit, st);
    ASSERT_EQ(1, nsend);
}

TEST(status_0x2000_to_non_mother_fails)
{
    status_$t st = 0;
    int16_t cmd[4] = { 0 };
    setup();
    send_status[0] = 0x110005;
    send_info[0].xmit_status = 0x2000;
    network_$send_request(net, 5, 1, cmd, 8, 0, 0, &retry, &timeout, &st);
    ASSERT_EQ(status_$network_too_many_transmit_retries, st);
    /* to the mother node it falls through to the retry count */
    setup();
    NETWORK_$MOTHER_NODE = 0x456;
    send_status[0] = 0x110005;
    send_info[0].xmit_status = 0x2000;
    network_$send_request(net, 5, 1, cmd, 8, 0, 0, &retry, &timeout, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, nsend);
}

TEST(retries_exhausted)
{
    status_$t st = 0;
    int16_t cmd[4] = { 0 };
    int i;
    setup();
    for (i = 0; i < 8; i++) { send_status[i] = 0x110005; }
    network_$send_request(net, 5, 1, cmd, 8, 0, 0, &retry, &timeout, &st);
    ASSERT_EQ(status_$network_too_many_transmit_retries, st);
    ASSERT_EQ(3, nsend);                /* retry hint 2: the third failure ends it */
    ASSERT_EQ(2, nwait);
}

/* ---- wait_response ---- */

static sock_$sock_t sock3;

static void wsetup(void)
{
    setup();
    SOCK_$DATA.socket_ptr[3] = &sock3;
    nwaitec = nrcv = ndump = 0;
    memset(wait_which, 0, sizeof(wait_which));
    memset(rcvs, 0, sizeof(rcvs));
    TIME_$CLOCKH = 0x1000;
}

TEST(wait_matches_reply)
{
    int32_t ec = 10;
    int16_t resp[0x60];
    int16_t rlen = 0;
    uint32_t bufs[4] = { 9, 9, 9, 9 };
    uint16_t dlen = 0;
    wsetup();
    rcvs[0] = (rcv_t){ 0, 0x42, 0x200, 0x400, 0x8000 };
    ASSERT_EQ(0xFF, (uint8_t)network_$wait_response(3, 0x42, 5, &ec, resp, &rlen, bufs, &dlen));
    ASSERT_EQ(11, ec);
    ASSERT_EQ(10, ec_val_seen);
    ASSERT_EQ(0x1005, deadline_seen);
    ASSERT_EQ(0xB8, rlen);              /* capped */
    ASSERT_EQ(0xB8, copy_len);
    ASSERT_EQ(0x400, dlen);
    ASSERT_EQ(0x1000, rtn_hdr_va);      /* rec.data & ~0x3FF */
    ASSERT_EQ(0x8000, bufs[0]);
    ASSERT_EQ(0x55, bufs[1]);
}

TEST(wait_skips_failed_and_stale_replies)
{
    int32_t ec = 0;
    int16_t resp[0x60];
    int16_t rlen = 0;
    uint32_t bufs[4];
    uint16_t dlen = 0;
    wsetup();
    rcvs[0] = (rcv_t){ 0x11000E, 0, 0, 0, 0 };
    rcvs[1] = (rcv_t){ 0, 0x41, 0x10, 0x400, 0x8000 };
    rcvs[2] = (rcv_t){ 0, 0x42, 0x10, 0, 0 };
    ASSERT_EQ(0xFF, (uint8_t)network_$wait_response(3, 0x42, 5, &ec, resp, &rlen, bufs, &dlen));
    ASSERT_EQ(3, ec);
    ASSERT_EQ(1, ndump);                /* the stale reply's page */
    ASSERT_EQ(0x10, rlen);
    ASSERT_EQ(0, bufs[0]);
}

TEST(wait_times_out)
{
    int32_t ec = 0;
    int16_t resp[4];
    int16_t rlen = 0;
    uint32_t bufs[4] = { 9 };
    uint16_t dlen = 0;
    wsetup();
    wait_which[0] = 1;
    ASSERT_EQ(0, network_$wait_response(3, 1, 5, &ec, resp, &rlen, bufs, &dlen));
    ASSERT_EQ(0, bufs[0]);
    ASSERT_EQ(0, ec);
}

int main(void)
{
    printf("network_$send_request / network_$wait_response\n");
    RUN_TEST(send_ok_first_time);
    RUN_TEST(build_failure_returns_header);
    RUN_TEST(retry_then_success_and_benign_bits);
    RUN_TEST(parity_error_returns_at_once);
    RUN_TEST(status_0x2000_to_non_mother_fails);
    RUN_TEST(retries_exhausted);
    RUN_TEST(wait_matches_reply);
    RUN_TEST(wait_skips_failed_and_stale_replies);
    RUN_TEST(wait_times_out);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
