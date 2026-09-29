/*
 * network/test/test_page_server.c - unit tests for NETWORK_$PAGE_SERVER
 * (0x00E11548) and its nested reply sender (0x00E10510)
 *
 * The server never returns: EC_$WAITN is scripted and longjmps back to the
 * test when the script runs out.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

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
#include "app/app.h"
#include "ast/ast.h"
#include "wp/wp.h"
#include "netbuf/netbuf.h"
#include "net_io/net_io.h"
#include "netlog/netlog.h"
#include "ring/ring.h"
#include "time/time.h"
#include "os/os.h"

/* ---- data ------------------------------------------------------------ */

MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);
pkt_$info_t NETWORK_$SERVER_PKT_INFO = { .flags = 8, .routing_type = 2 };
void *NETWORK_$LOCK;
uint32_t NETWORK_$ALLOWED_SERVICE;
uint32_t NETWORK_$ZERO_PAGE_PA;
uint32_t NETWORK_$PAGE_WAIT[4];
uint32_t NETWORK_$RING_DCTE;
uint16_t NETWORK_$FILE_OVER_CNT;
uint16_t NETWORK_$REPLY_SEND_FLAGS;
int16_t  NETWORK_$SERVICE_TIME;
uint16_t NETWORK_$2LONG1;
ec_$eventcount_t NETLOG_$EC;
uint32_t TIME_$CLOCKH;
static sock_$sock_t sock1, sock6;

/* ---- mocks ----------------------------------------------------------- */

static jmp_buf out;
static int16_t wait_script[8];
static int n_wait, waits_done;
static int32_t ec_read_value[4];
static int crash_calls, zero_calls, open_calls, ppr_calls, netlog_calls;
static int receive_calls, rtn_hdr_calls, dump_calls, gethdr_calls, send_calls;
static int rtnhdr_calls, report_calls;
static uint16_t open_socks[4];
static int8_t sticky_result;
static status_$t send_status[8];
static int n_send;
static uint16_t bld_retries;
static uint16_t bld_tlen;
static uint32_t bld_dest;
static uint8_t sent_reply[4];
static uint16_t report_word;
static network_$ps_frame_t *ppr_frame;
static uint8_t arena[0x800];
static app_$receive_rec_t rcv_stub;

ml_$spin_token_t ML_$SPIN_LOCK(void *l) { (void)l; return 7; }
void ML_$SPIN_UNLOCK(void *l, ml_$spin_token_t t) { (void)l; (void)t; }
uint32_t MMAP_$REMOTE_POOL(uint32_t p) { return p + 1; }
void WP_$CALLOC(uint32_t *ppn, status_$t *st) { *ppn = 0x123; *st = 0; }
void CRASH_SYSTEM(const status_$t *s) { (void)s; crash_calls++; }
void AST_$PAGE_ZERO(uint32_t ppn) { (void)ppn; zero_calls++; }
int8_t SOCK_$OPEN(uint16_t s, uint32_t a, uint32_t b)
{ (void)a; (void)b; open_socks[open_calls++ & 3] = s; return -1; }
void PROC1_$SET_LOCK(uint16_t id) { (void)id; }
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *vals, int16_t n)
{
    (void)ecs; (void)vals; (void)n;
    if (waits_done >= n_wait) {
        longjmp(out, 1);
    }
    return (uint16_t)(wait_script[waits_done++] + 1);
}
int32_t EC_$READ(ec_$eventcount_t *ec)
{
    if (ec == &sock6.ec) return ec_read_value[0];
    if (ec == &sock1.ec) return ec_read_value[1];
    if (ec == &NETLOG_$EC) return ec_read_value[2];
    return ec_read_value[3];
}
void APP_$RECEIVE(uint16_t s, void *r, status_$t *st)
{ (void)s; receive_calls++; memcpy(r, &rcv_stub, sizeof(rcv_stub)); *st = 0; }
void OS_$DATA_COPY(const void *s, void *d, uint32_t l) { memcpy(d, s, l); }
void NETBUF_$RTN_HDR(uint32_t *va) { (void)va; rtn_hdr_calls++; }
void PKT_$DUMP_DATA(uint32_t *b, int16_t l) { (void)b; (void)l; dump_calls++; }
void NETWORK_$GETHDR(uint32_t *node, uint32_t *va, uint32_t *pa)
{ (void)node; gethdr_calls++; *va = 0x9000; *pa = 0x4000; }
void NETWORK_$RTNHDR(uint32_t *va) { (void)va; rtnhdr_calls++; }
void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           const pkt_$info_t *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len, uint16_t data_len,
                           int16_t *port_out, pkt_$hdr_t *hdr, uint16_t *len_out,
                           uint16_t *retry_hint, uint16_t *timeout_out,
                           status_$t *status_ret)
{
    (void)routing_key; (void)dest_sock; (void)src_node_or; (void)src_node;
    (void)src_sock; (void)pkt_info; (void)request_id; (void)data_len; (void)hdr;
    bld_dest = dest_node;
    bld_tlen = template_len;
    memcpy(sent_reply, template, 4);
    *port_out = 1; *len_out = 0x30; *retry_hint = bld_retries; *timeout_out = 5;
    *status_ret = 0;
}
void NET_IO_$SEND(int16_t port, uint32_t *hdr_ptr, uint32_t hdr_pa,
                  uint16_t hdr_len, uint32_t data_va, uint32_t *data_pages,
                  int16_t data_len, uint16_t flags,
                  net_io_$send_info_t *send_info, status_$t *status_ret)
{
    (void)port; (void)hdr_ptr; (void)hdr_pa; (void)hdr_len; (void)data_va;
    (void)data_pages; (void)data_len; (void)flags; (void)send_info;
    *status_ret = send_status[send_calls++ & 7];
}
void NETWORK_$PROCESS_PAGING_REQUEST(network_$ps_frame_t *ps) { ppr_calls++; ppr_frame = ps; }
void NETLOG_$SEND_PAGE(void) { netlog_calls++; }
int8_t RING_$POLL_STICKY_BPHERR(uint32_t *c) { (void)c; return sticky_result; }
void NETWORK_$REPORT_FAILURE(uint16_t *w) { report_calls++; report_word = *w; }

#include "../page_server.c"

/* ---- helpers --------------------------------------------------------- */

static void reset_state(void)
{
    memset(&MMAP_$DATA, 0, sizeof(MMAP_$DATA));
    memset(NETWORK_$PAGE_WAIT, 0, sizeof(NETWORK_$PAGE_WAIT));
    memset(ec_read_value, 0, sizeof(ec_read_value));
    memset(send_status, 0, sizeof(send_status));
    SOCK_$DATA.socket_ptr[1] = &sock1;
    SOCK_$DATA.socket_ptr[6] = &sock6;
    MMAP_$MIN_RMT_POOL = 0x10;
    TIME_$CLOCKH = 100;
    NETWORK_$RING_DCTE = 0;
    NETWORK_$FILE_OVER_CNT = 0;
    NETWORK_$2LONG1 = 0;
    n_wait = waits_done = 0;
    crash_calls = zero_calls = open_calls = ppr_calls = netlog_calls = 0;
    receive_calls = rtn_hdr_calls = dump_calls = gethdr_calls = send_calls = 0;
    rtnhdr_calls = report_calls = 0;
    bld_retries = 3;
    sticky_result = 0;
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x10000u;
}

static void run(void)
{
    if (setjmp(out) == 0) {
        NETWORK_$PAGE_SERVER();
    }
}

/* ---- tests ----------------------------------------------------------- */

static void test_startup(void)
{
    run();
    ASSERT_EQ(0x32, MMAP_$MIN_RMT_POOL);
    ASSERT_EQ(0x32, MMAP_$PAGEABLE_PAGES_LOWER_LIMIT);
    ASSERT_EQ(0x123u << 10, NETWORK_$ZERO_PAGE_PA);
    ASSERT_EQ(1, zero_calls);
    ASSERT_EQ(2, open_calls);
    ASSERT_EQ(1, open_socks[0]);
    ASSERT_EQ(6, open_socks[1]);
    ASSERT_EQ(1, NETWORK_$PAGE_WAIT[0]);
    ASSERT_EQ(104, NETWORK_$PAGE_WAIT[3]);     /* clock + 4 */
}

static void test_paging_and_netlog_slots(void)
{
    wait_script[n_wait++] = 1;
    wait_script[n_wait++] = 2;
    ec_read_value[0] = ec_read_value[1] = ec_read_value[2] = ec_read_value[3] = -1000;
    run();
    ASSERT_EQ(1, ppr_calls);
    ASSERT_EQ(1, netlog_calls);
    ASSERT_EQ(2, NETWORK_$PAGE_WAIT[1]);
    ASSERT_EQ(2, NETWORK_$PAGE_WAIT[2]);
}

static void test_scan_serves_ready_later_slot(void)
{
    wait_script[n_wait++] = 0;
    ec_read_value[0] = -100;                    /* slot 0 not ready again */
    ec_read_value[1] = 5;                       /* slot 1 ready */
    rcv_stub.reply = 0x10000;
    run();
    ASSERT_EQ(1, receive_calls);
    ASSERT_EQ(1, ppr_calls);                   /* reached through the scan */
}

static void test_overflow_request_refused(void)
{
    network_$rcv_rec_t *r = (network_$rcv_rec_t *)(void *)arena;
    wait_script[n_wait++] = 0;
    memset(arena, 0, sizeof(arena));
    r->rqst_len = 2;
    r->node_b = 0x777;
    rcv_stub.reply = 0x10000;
    rcv_stub.data = 0x10100;
    rcv_stub.data_pages[0] = 0x2000;
    run();
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(1, dump_calls);
    ASSERT_EQ(4, bld_tlen);
    ASSERT_EQ(0x777, bld_dest);
    ASSERT_EQ(0xFF, sent_reply[0]);
    ASSERT_EQ(0xFF, sent_reply[3]);
    ASSERT_EQ(1, NETWORK_$FILE_OVER_CNT);
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(1, rtnhdr_calls);
}

static void test_send_reply_retries(void)
{
    wait_script[n_wait++] = 0;
    rcv_stub.reply = 0x10000;
    send_status[0] = 0x00110004;
    send_status[1] = 0x00110004;
    send_status[2] = 0x00110004;
    run();
    ASSERT_EQ(3, gethdr_calls);                /* bld's retry hint is 3 */
    ASSERT_EQ(3, send_calls);
    ASSERT_EQ(3, rtnhdr_calls);
}

static void test_clock_sticky_errors(void)
{
    int i;
    NETWORK_$RING_DCTE = 0x1234;
    sticky_result = (int8_t)0xFF;
    for (i = 0; i < 5; i++) {
        wait_script[n_wait++] = 3;
    }
    ec_read_value[3] = -1000;
    run();
    ASSERT_EQ(1, report_calls);
    ASSERT_EQ(0x0020, report_word);
}

int main(void)
{
    printf("NETWORK_$PAGE_SERVER tests:\n");
    RUN_TEST(startup);
    RUN_TEST(paging_and_netlog_slots);
    RUN_TEST(scan_serves_ready_later_slot);
    RUN_TEST(overflow_request_refused);
    RUN_TEST(send_reply_retries);
    RUN_TEST(clock_sticky_errors);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
