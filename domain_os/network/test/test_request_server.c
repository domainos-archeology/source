/*
 * network/test/test_request_server.c - unit tests for
 * NETWORK_$REQUEST_SERVER (0x00E118DC)
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
#include "asknode/asknode.h"
#include "rem_file/rem_file.h"
#include "rip/rip.h"
#include "app/app.h"
#include "ring/ring.h"
#include "route/route.h"
#include "time/time.h"
#include "node/node.h"

/* ---- data ------------------------------------------------------------ */

MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);
MODULE_DATA_DEFINE(ring_$wired_data_t, RING_$WIRED_DATA, 0x00E261AC);
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
pkt_$info_t NETWORK_$SERVER_PKT_INFO = { .flags = 8 };
void *NETWORK_$LOCK;
uint32_t NETWORK_$ALLOWED_SERVICE;
uint32_t NETWORK_$RQST_WAIT[4];
uint32_t NETWORK_$RING_DCTE;
uint16_t NETWORK_$AGE_TICKS;
int8_t   NETWORK_$STD_OPEN_FLAG;
int8_t   NETWORK_$ACTIVITY_FLAG;
network_$failure_rec_t NETWORK_$FAILURE_REC;
ec_$eventcount_t NETWORK_$RQST_DONE_EC, NETWORK_$RQST_QUIT_EC;
ml_$exclusion_t REM_FILE_$SOCK_LOCK;
const uint32_t network_$c_zero_long = 0;
uint32_t TIME_$CLOCKH;
uint32_t NODE_$ME;
uint16_t PROC1_$CURRENT;
static sock_$sock_t sock2, sock4, sock8;

/* ---- mocks ----------------------------------------------------------- */

static jmp_buf out;
static int16_t wait_script[16];
static int n_wait, waits_done;
static int open_calls, std_rip, std_app, rem_calls, rip_calls, age_calls;
static int ask_calls, prop_calls, adv_calls, cancel_calls, sendp_calls;
static int report_calls, crash_calls, unbind_calls, done_adv;
static uint16_t ask_set_version;
static status_$t adv_status;
static uint16_t sendp_result[4];

ml_$spin_token_t ML_$SPIN_LOCK(void *l) { (void)l; return 1; }
void ML_$SPIN_UNLOCK(void *l, ml_$spin_token_t t) { (void)l; (void)t; }
uint32_t MMAP_$REMOTE_POOL(uint32_t p) { return p; }
int8_t SOCK_$OPEN(uint16_t s, uint32_t a, uint32_t b) { (void)s; (void)a; (void)b; open_calls++; return -1; }
void RIP_$STD_OPEN(void) { std_rip++; }
void APP_$STD_OPEN(void) { std_app++; }
void EC_$INIT(ec_$eventcount_t *ec) { ec->value = 0; }
void PROC1_$INHIBIT_BEGIN(void) {}
void PROC1_$INHIBIT_END(void) {}
void ML_$EXCLUSION_INIT(ml_$exclusion_t *e) { (void)e; }
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *vals, int16_t n)
{
    (void)ecs; (void)vals; (void)n;
    if (waits_done >= n_wait) {
        longjmp(out, 1);
    }
    return (uint16_t)(wait_script[waits_done++] + 1);
}
int32_t EC_$READ(ec_$eventcount_t *ec) { (void)ec; return -100000; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { if (ec == &NETWORK_$RQST_DONE_EC) done_adv++; }
void REM_FILE_$SERVER(void) { rem_calls++; }
void RIP_$SERVER(void) { rip_calls++; }
void RIP_$AGE(void) { age_calls++; }
void ASKNODE_$SERVER(asknode_$server_ctx_t *ctx, int32_t *r)
{ (void)r; ask_calls++; ctx->version = ask_set_version; }
void ASKNODE_$PROPAGATE_WHO(int16_t *resp, uint32_t *r) { (void)resp; (void)r; prop_calls++; }
void TIME_$ADVANCE(uint16_t *rel, clock_t *when, ec_$eventcount_t *ec,
                   time_queue_elem_t *elem, status_$t *st)
{ (void)rel; (void)when; (void)ec; elem->callback_arg = 1; adv_calls++; *st = adv_status; }
void TIME_$CANCEL(int32_t v, time_queue_elem_t *e, status_$t *st) { (void)v; (void)e; cancel_calls++; *st = 0; }
void NETWORK_$GETHDR(uint32_t *n, uint32_t *va, uint32_t *pa) { (void)n; *va = 0x100; *pa = 0x200; }
void NETWORK_$RTNHDR(uint32_t *va) { (void)va; }
void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           const pkt_$info_t *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len, uint16_t data_len,
                           int16_t *port_out, pkt_$hdr_t *hdr, uint16_t *len_out,
                           uint16_t *retry_hint, uint16_t *timeout_out,
                           status_$t *status_ret)
{
    (void)routing_key; (void)dest_node; (void)dest_sock; (void)src_node_or;
    (void)src_node; (void)src_sock; (void)pkt_info; (void)request_id;
    (void)template; (void)template_len; (void)data_len; (void)hdr;
    *port_out = 0; *len_out = 0x20; *retry_hint = 0; *timeout_out = 0;
    *status_ret = 0;
}
void RING_$SENDP(uint16_t *unit_ptr, uint32_t hdr_pa, ring_$pkt_hdr_t *hdr,
                 uint16_t hdr_len, const uint32_t *data_desc,
                 uint32_t unused_1a, uint16_t data_len,
                 const uint16_t *send_opts, uint16_t *result_flags,
                 status_$t *status_ret)
{
    (void)unit_ptr; (void)hdr_pa; (void)hdr; (void)hdr_len; (void)data_desc;
    (void)unused_1a; (void)data_len; (void)send_opts;
    *result_flags = sendp_result[sendp_calls++ & 3];
    *status_ret = 0;
}
void NETWORK_$REPORT_FAILURE(uint16_t *w) { (void)w; report_calls++; }
void CRASH_SYSTEM(const status_$t *s) { (void)s; crash_calls++; }
void PROC1_$UNBIND(uint16_t pid, status_$t *st) { (void)pid; unbind_calls++; *st = 0; }

#include "../request_server.c"

/* ---- helpers --------------------------------------------------------- */

static void reset_state(void)
{
    memset(&MMAP_$DATA, 0, sizeof(MMAP_$DATA));
    memset(NETWORK_$RQST_WAIT, 0, sizeof(NETWORK_$RQST_WAIT));
    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    memset(&RING_$WIRED_DATA, 0, sizeof(RING_$WIRED_DATA));
    memset(&NETWORK_$FAILURE_REC, 0, sizeof(NETWORK_$FAILURE_REC));
    SOCK_$DATA.socket_ptr[2] = &sock2;
    SOCK_$DATA.socket_ptr[4] = &sock4;
    SOCK_$DATA.socket_ptr[8] = &sock8;
    ROUTE_$WIRED_DATA.portp[0] = &ROUTE_$PORT_ARRAY[0];
    MMAP_$MIN_RMT_POOL = 5;
    TIME_$CLOCKH = 1000;
    NODE_$ME = 0x42;
    NETWORK_$STD_OPEN_FLAG = (int8_t)0xFF;
    NETWORK_$AGE_TICKS = 0;
    NETWORK_$RING_DCTE = 0;
    NETWORK_$ACTIVITY_FLAG = 0;
    n_wait = waits_done = 0;
    open_calls = std_rip = std_app = rem_calls = rip_calls = age_calls = 0;
    ask_calls = prop_calls = adv_calls = cancel_calls = sendp_calls = 0;
    report_calls = crash_calls = unbind_calls = done_adv = 0;
    ask_set_version = 0xDEAF;
    adv_status = 0;
    memset(sendp_result, 0, sizeof(sendp_result));
}

static void run(void)
{
    if (setjmp(out) == 0) {
        NETWORK_$REQUEST_SERVER();
    }
}

/* ---- tests ----------------------------------------------------------- */

static void test_startup(void)
{
    run();
    ASSERT_EQ(6, MMAP_$MIN_RMT_POOL);
    ASSERT_EQ(3, open_calls);
    ASSERT_EQ(1, std_rip);
    ASSERT_EQ(1, std_app);
    ASSERT_EQ(0, NETWORK_$STD_OPEN_FLAG);
    ASSERT_EQ(1, NETWORK_$RQST_WAIT[0]);
    ASSERT_EQ(1060, NETWORK_$RQST_WAIT[3]);    /* clock + 0x3C */
}

static void test_file_and_rip_slots(void)
{
    wait_script[n_wait++] = 0;
    wait_script[n_wait++] = 2;
    run();
    ASSERT_EQ(1, rem_calls);
    ASSERT_EQ(1, rip_calls);
    ASSERT_EQ(2, NETWORK_$RQST_WAIT[0]);
    ASSERT_EQ(2, NETWORK_$RQST_WAIT[2]);
}

static void test_asknode_plain(void)
{
    wait_script[n_wait++] = 1;
    run();
    ASSERT_EQ(1, ask_calls);
    ASSERT_EQ(0, adv_calls);
    ASSERT_EQ(2, NETWORK_$RQST_WAIT[1]);
}

static void test_asknode_delayed_propagation(void)
{
    ask_set_version = 1;
    wait_script[n_wait++] = 1;                  /* WHO: arm the timer */
    wait_script[n_wait++] = 1;                  /* the timer fired */
    run();
    ASSERT_EQ(1, ask_calls);
    ASSERT_EQ(1, adv_calls);
    ASSERT_EQ(1, prop_calls);
    ASSERT_EQ(1, NETWORK_$RQST_WAIT[1] - 1);   /* only the propagation relocked */
}

static void test_asknode_advance_failure_propagates_now(void)
{
    ask_set_version = 1;
    adv_status = 0x000D0001;
    wait_script[n_wait++] = 1;
    run();
    ASSERT_EQ(1, prop_calls);
}

static void test_clock_ages_and_rearms(void)
{
    wait_script[n_wait++] = 3;
    run();
    ASSERT_EQ(1, age_calls);
    ASSERT_EQ(1, NETWORK_$AGE_TICKS);
    ASSERT_EQ(1120, NETWORK_$RQST_WAIT[3]);    /* rearmed twice */
    ASSERT_EQ(0, sendp_calls);
}

static void test_ring_check_reports_failure(void)
{
    NETWORK_$AGE_TICKS = 239;                   /* the next tick is 240 */
    NETWORK_$RING_DCTE = 1;
    ROUTE_$PORT_ARRAY[0].active = 0;
    TIME_$CLOCKH = 10000;
    NETWORK_$FAILURE_REC.timestamp = 0;
    sendp_result[0] = sendp_result[1] = sendp_result[2] = 0x0020;
    wait_script[n_wait++] = 3;
    run();
    ASSERT_EQ(3, sendp_calls);
    ASSERT_EQ(1, report_calls);
    ASSERT_EQ(0, NETWORK_$ACTIVITY_FLAG);
}

static void test_ring_check_success_stores_flag(void)
{
    NETWORK_$AGE_TICKS = 239;
    NETWORK_$RING_DCTE = 1;
    TIME_$CLOCKH = 10000;
    NETWORK_$FAILURE_REC.flag = (int8_t)0xFF;
    wait_script[n_wait++] = 3;
    run();
    ASSERT_EQ(1, sendp_calls);
    ASSERT_EQ(0, report_calls);
    ASSERT_EQ(0, NETWORK_$FAILURE_REC.flag);   /* ~report & flag */
}

static void test_no_ring_check_when_recent_foreign_failure(void)
{
    NETWORK_$AGE_TICKS = 239;
    NETWORK_$RING_DCTE = 1;
    TIME_$CLOCKH = 100;
    NETWORK_$FAILURE_REC.timestamp = 50;       /* within 0x157 */
    NETWORK_$FAILURE_REC.node_id = 7;          /* not this node */
    NETWORK_$FAILURE_REC.flag = (int8_t)0xFF;
    wait_script[n_wait++] = 3;
    run();
    ASSERT_EQ(0, sendp_calls);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)NETWORK_$FAILURE_REC.flag);
}

static void test_quit(void)
{
    wait_script[n_wait++] = 4;
    run();
    ASSERT_EQ(1, done_adv);
    ASSERT_EQ(1, unbind_calls);
    ASSERT_EQ(1, crash_calls);
}

int main(void)
{
    printf("NETWORK_$REQUEST_SERVER tests:\n");
    RUN_TEST(startup);
    RUN_TEST(file_and_rip_slots);
    RUN_TEST(asknode_plain);
    RUN_TEST(asknode_delayed_propagation);
    RUN_TEST(asknode_advance_failure_propagates_now);
    RUN_TEST(clock_ages_and_rearms);
    RUN_TEST(ring_check_reports_failure);
    RUN_TEST(ring_check_success_stores_flag);
    RUN_TEST(no_ring_check_when_recent_foreign_failure);
    RUN_TEST(quit);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
