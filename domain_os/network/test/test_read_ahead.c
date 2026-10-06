/*
 * network/test/test_read_ahead.c - unit tests for NETWORK_$READ_AHEAD
 * (0x00E0FC08)
 *
 * network_$wait_response is scripted: each call consumes the next entry of
 * `script` (a reply or a timeout).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "network/network_internal.h"
#include "pkt/pkt.h"
#include "wp/wp.h"
#include "netbuf/netbuf.h"
#include "route/route.h"

/* ---- data ------------------------------------------------------------ */

uint32_t NETWORK_$ALLOWED_SERVICE;
uint32_t NETWORK_$MOTHER_NODE;
int16_t  NETWORK_$SERVICE_TIME;
char     NETWORK_$DO_CHKSUM;
uint16_t NETWORK_$BAD_CHKSUM_CNT;
uint16_t NETWORK_$READ_CALL_CNT;
uint16_t NETWORK_$RCV_READ_AHEAD;
uint32_t NETWORK_$ZERO_PAGE_PA;
uint32_t ROUTE_$PORT;
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);
static sock_$sock_t the_sock;

/* ---- mocks ----------------------------------------------------------- */

typedef struct {
    int8_t result;                  /* < 0 reply, >= 0 timeout */
    network_$pagin_reply_t resp;
    uint16_t data_len;
    uint32_t bufs[4];
} script_t;

static script_t script[8];
static int n_script, waits;
static int crash_calls, sends, dumps, closes, rtn_dat, get_dat, frees, callocs;
static int copies;
static uint32_t copy_dst[8], copy_src[8];
static int16_t copy_len[8];
static uint16_t retry_max_stub;
static uint32_t chksum_stub;
static int8_t alloc_result;
static network_$pagin_rqst_t last_rqst;
static uint32_t next_dat;

void CRASH_SYSTEM(const status_$t *s) { (void)s; crash_calls++; }
int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t p, uint32_t q)
{ (void)p; (void)q; *sock_ret = 5; return alloc_result; }
void SOCK_$CLOSE(uint16_t s) { (void)s; closes++; }
int16_t PKT_$NEXT_ID(void) { return 0x77; }
void PKT_$DUMP_DATA(uint32_t *b, int16_t l) { (void)b; (void)l; dumps++; }
ulong M$DIU$LLW(ulong a, ushort b) { return a / b; }
short M$OIS$WLW(long a, short b) { return (short)(a % b); }
void WP_$CALLOC(uint32_t *ppn, status_$t *st) { *ppn = 0x300 + callocs++; *st = 0; }
void NETBUF_$RTN_DAT(uint32_t a) { (void)a; rtn_dat++; }
void NETBUF_$GET_DAT(uint32_t *a) { *a = next_dat; next_dat += 0x400; get_dat++; }
void MMAP_$FREE(uint32_t v) { (void)v; frees++; }
uint32_t network_$page_chksum(uint32_t *b) { (void)b; return chksum_stub; }
void network_$phys_copy(uint32_t d, uint32_t s, int16_t l)
{ copy_dst[copies & 7] = d; copy_src[copies & 7] = s; copy_len[copies & 7] = l; copies++; }

void network_$send_request(void *h, int16_t s, int16_t id, int16_t *buf,
                           int16_t len, uint32_t hi, uint16_t lo,
                           uint16_t *retry, int16_t *timeout, status_$t *st)
{
    (void)h; (void)s; (void)id; (void)hi; (void)lo;
    sends++;
    memcpy(&last_rqst, buf, sizeof(last_rqst));
    ASSERT_EQ(0x2E, len);
    *retry = retry_max_stub;
    *timeout = 10;
    *st = 0;
}

int8_t network_$wait_response(int16_t s, int16_t id, uint16_t timeout,
                              int32_t *ec, int16_t *resp, int16_t *resp_len,
                              uint32_t *bufs, uint16_t *data_len)
{
    script_t *e;
    (void)s; (void)id; (void)timeout; (void)ec;
    if (waits >= n_script) {
        waits++;
        return 0;
    }
    e = &script[waits++];
    memcpy(resp, &e->resp, sizeof(e->resp));
    *resp_len = (int16_t)sizeof(e->resp);
    memcpy(bufs, e->bufs, sizeof(e->bufs));
    *data_len = e->data_len;
    return e->result;
}

#include "../read_ahead.c"

/* ---- helpers --------------------------------------------------------- */

static uint32_t net[2];
static network_$page_request_t req;
static uint32_t ppns[8];
static clock_t dtm, clk, acl;
static status_$t st;

static void reset_state(void)
{
    memset(script, 0, sizeof(script));
    n_script = waits = 0;
    crash_calls = sends = dumps = closes = rtn_dat = get_dat = frees = callocs = 0;
    copies = 0;
    retry_max_stub = 2;
    chksum_stub = 0;
    alloc_result = (int8_t)0xFF;
    next_dat = 0x40000;
    NETWORK_$ALLOWED_SERVICE = (uint32_t)NETWORK_SERVICE_PAGING << 16;
    NETWORK_$MOTHER_NODE = 0x999;
    NETWORK_$DO_CHKSUM = 0;
    NETWORK_$BAD_CHKSUM_CNT = NETWORK_$READ_CALL_CNT = NETWORK_$RCV_READ_AHEAD = 0;
    NETWORK_$ZERO_PAGE_PA = 0x7000;
    ROUTE_$PORT = 0x10;
    SOCK_$DATA.socket_ptr[5] = &the_sock;
    net[0] = 0x10; net[1] = 0x123;
    memset(&req, 0, sizeof(req));
    req.uid.high = 0xAA; req.page_num = 100;
    memset(ppns, 0xEE, sizeof(ppns));
    dtm.high = 0x55; dtm.low = 0;
    clk.high = clk.low = acl.high = acl.low = 0;
    st = 0x99;
}

static script_t *reply(int16_t seq, int16_t cnt, uint16_t len, status_$t s)
{
    script_t *e = &script[n_script++];
    e->result = (int8_t)0xFF;
    e->resp.type = 0xD;
    e->resp.status = s;
    e->resp.version = 7;
    e->resp.seq = seq;
    e->resp.page_cnt = cnt;
    e->resp.req.uid.high = 0xBB;
    e->resp.req.page_num = 0x1234;
    e->resp.dtm_high = 0xD1; e->resp.dtm_low = 0xD2;
    e->resp.clock_high = 0xC1; e->resp.clock_low = 0xC2;
    e->resp.acl_high = 0xA1; e->resp.acl_low = 0xA2;
    e->data_len = len;
    return e;
}

static void timeout_entry(void) { script[n_script++].result = 0; }

static int16_t run(int16_t count, uint16_t page_size, int8_t flag_1a)
{
    return NETWORK_$READ_AHEAD(net, &req, ppns, page_size, count, 0,
                               (uint8_t)flag_1a, &dtm, &clk, &acl, &st);
}

/* ---- tests ----------------------------------------------------------- */

static void test_paging_not_allowed(void)
{
    NETWORK_$ALLOWED_SERVICE = 0;
    ASSERT_EQ(0, run(2, 0x400, 0));
    ASSERT_EQ(status_$network_request_denied_by_local_node, st);
    ASSERT_EQ(0, dtm.high);
    ASSERT_EQ(0, sends);
}

static void test_two_pages_one_packet(void)
{
    script_t *e = reply(1, 1, 0x800, 0);
    e->bufs[0] = 0x1000; e->bufs[1] = 0x2000;
    ASSERT_EQ(2, run(2, 0x400, 0));
    ASSERT_EQ(0, st);
    ASSERT_EQ(4, ppns[0]);
    ASSERT_EQ(8, ppns[1]);
    ASSERT_EQ(0x20, last_rqst.limit);          /* local network */
    ASSERT_EQ(0xC, last_rqst.type);
    ASSERT_EQ(100, last_rqst.req.page_num);
    ASSERT_EQ(2, last_rqst.count);
    ASSERT_EQ(0xBB, req.uid.high);             /* record copied back */
    ASSERT_EQ(0xC1, clk.high);
    ASSERT_EQ(0xA2, acl.low);
    ASSERT_EQ(0xD1, dtm.high);
    ASSERT_EQ(2, NETWORK_$READ_CALL_CNT);
    ASSERT_EQ(1, NETWORK_$RCV_READ_AHEAD);
    ASSERT_EQ(1, closes);
    ASSERT_EQ(0, crash_calls);
}

static void test_remote_network_limit_and_two_packets(void)
{
    net[0] = 0x20;
    reply(1, 2, 0x400, 0)->bufs[0] = 0x1000;
    reply(2, 2, 0x400, 0)->bufs[0] = 0x3000;
    ASSERT_EQ(2, run(2, 0x400, 0));
    ASSERT_EQ(8, last_rqst.limit);
    ASSERT_EQ(4, ppns[0]);
    ASSERT_EQ(0xC, ppns[1]);
    ASSERT_EQ(1, sends);
}

static void test_timeouts_fail_remote_node(void)
{
    retry_max_stub = 1;
    ASSERT_EQ(0, run(1, 0x400, 0));
    ASSERT_EQ(status_$network_remote_node_failed_to_respond, st);
    ASSERT_EQ(2, sends);
    ASSERT_EQ(1, closes);
}

static void test_bad_length_retries(void)
{
    reply(1, 1, 0x200, 0);
    reply(1, 1, 0x400, 0)->bufs[0] = 0x5000;
    ASSERT_EQ(1, run(1, 0x400, 0));
    ASSERT_EQ(1, dumps);
    ASSERT_EQ(2, sends);
    ASSERT_EQ(0x14, ppns[0]);
}

static void test_wrong_reply_type(void)
{
    script_t *e = reply(1, 1, 0x400, 0);
    e->resp.type = 0x7;
    ASSERT_EQ(0, run(1, 0x400, 0));
    ASSERT_EQ(status_$network_unexpected_reply_type, st);
}

static void test_eof_status(void)
{
    reply(1, 1, 0, status_$ast_eof);
    ASSERT_EQ(0, run(1, 0x400, 0));
    ASSERT_EQ(status_$ast_eof, st);
}

static void test_null_pages_first(void)
{
    script_t *e = reply(1, 3, 0, NETWORK_PAGIN_ST_NULL_PAGES);
    e->resp.version = 2;
    ASSERT_EQ(3, run(3, 0x400, 0));
    ASSERT_EQ(0, ppns[0]);
    ASSERT_EQ(NETWORK_PAGIN_ST_NULL_PAGES, st);
}

static void test_null_pages_zero_filled_after_data(void)
{
    reply(1, 2, 0x400, 0)->bufs[0] = 0x1000;
    reply(2, 2, 0, NETWORK_PAGIN_ST_NULL_PAGES)->resp.version = 7;
    script[1].resp.page_cnt = 1;
    ASSERT_EQ(2, run(2, 0x400, 0));
    ASSERT_EQ(1, get_dat);
    ASSERT_EQ(1, copies);
    ASSERT_EQ(0x7000, copy_src[0]);
    ASSERT_EQ(0x400, copy_len[0]);
    ASSERT_EQ(0x40000 >> 10, ppns[1]);
}

static void test_checksum_mismatch(void)
{
    NETWORK_$DO_CHKSUM = (char)0x80;
    reply(1, 1, 0x400, 0)->resp.chksum = 0x1234;
    reply(1, 1, 0x400, 0)->resp.chksum = 0x5678;
    script[1].bufs[0] = 0x1000;
    chksum_stub = 0x5678;
    ASSERT_EQ(1, run(1, 0x400, 0));
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(1, NETWORK_$BAD_CHKSUM_CNT);
    ASSERT_EQ(1, dumps);
}

static void test_donates_and_reclaims_buffers(void)
{
    reply(1, 1, 0x800, 0);
    script[0].bufs[0] = 0x1000; script[0].bufs[1] = 0x2000;
    ASSERT_EQ(2, run(2, 0x200, 0));            /* 4 half-pages - 2 = 2 extra */
    ASSERT_EQ(2, callocs);
    ASSERT_EQ(2, rtn_dat);
    ASSERT_EQ(2, get_dat);
    ASSERT_EQ(2, frees);
}

static void test_socket_failure_crashes(void)
{
    alloc_result = 0;
    reply(1, 1, 0x400, 0)->bufs[0] = 0x1000;
    ASSERT_EQ(1, run(1, 0x400, 0));
    ASSERT_EQ(1, crash_calls);
}

int main(void)
{
    printf("NETWORK_$READ_AHEAD tests:\n");
    RUN_TEST(paging_not_allowed);
    RUN_TEST(two_pages_one_packet);
    RUN_TEST(remote_network_limit_and_two_packets);
    RUN_TEST(timeouts_fail_remote_node);
    RUN_TEST(bad_length_retries);
    RUN_TEST(wrong_reply_type);
    RUN_TEST(eof_status);
    RUN_TEST(null_pages_first);
    RUN_TEST(null_pages_zero_filled_after_data);
    RUN_TEST(checksum_mismatch);
    RUN_TEST(donates_and_reclaims_buffers);
    RUN_TEST(socket_failure_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
