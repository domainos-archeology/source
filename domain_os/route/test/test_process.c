/*
 * route/test/test_process.c - unit tests for ROUTE_$PROCESS (0x00E873EC)
 *
 * These tests #include route/process.c itself and drive the real
 * ROUTE_$PROCESS through mocked callees, so the decision logic under test is
 * the emitted translation and not a re-implementation.  Each test scripts one
 * EC_$WAIT wakeup on the socket index followed by a wakeup on the control
 * index, which is how the original leaves its loop (0x00E8749A / 0x00E877FE).
 *
 * The behaviours pinned down here are the ones the 2026-09-06 audit found
 * wrong in the previous translation:
 *
 *   - is_std_routing comes from bit 1 of the SOCK_$GET record's flags byte,
 *     and should_forward is that OR (header routing type >= 2) - 0x00E874EA;
 *   - the IDP header sits at the packet base for standard routing and at
 *     +0x28 otherwise - 0x00E87500;
 *   - a non-standard packet with a non-zero data length but no data pages is
 *     dropped against the 0xE87FBC counter - 0x00E8750C;
 *   - the hop limit fires at >= 0x10, not > 0x10 - 0x00E8754A;
 *   - the port "active" word is tested with a 0x30 mask for standard routing
 *     and 0x28 otherwise - 0x00E875C4 / 0x00E875D6;
 *   - a type 2 (user routing) port gets SOCK_$PUT and the per-port stats,
 *     with the socket queue depth bucketed at 0x20 - 0x00E87618;
 *   - the forwarded counters are per routing mode - 0x00E87778;
 *   - buffers are returned only when the packet was not handed off, and
 *     NETBUF_$RTN_HDR gets a copy of the header pointer - 0x00E87792;
 *   - the shutdown path unwires ascending from index 0 - 0x00E87856.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }           \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                    \
        return;                                                                \
    }                                                                          \
} while (0)

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                             \
        printf("FAILED\n    Assertion failed at line %d: %s\n",                \
               __LINE__, #cond);                                               \
        tests_failed++; current_failed = 1;                                     \
        return;                                                                 \
    }                                                                           \
} while (0)

/* ==========================================================================
 * Headers the translation unit under test needs
 * ========================================================================== */

#include "route/route_internal.h"
#include "ec/ec.h"
#include "proc1/proc1.h"
#include "sock/sock.h"
#include "rip/rip.h"
#include "rip/rip.h"
#include "time/time.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"
#include "net_io/net_io.h"
#include "network/network.h"
#include "mac_os/mac_os.h"
#include "xns/xns.h"
#include "wp/wp.h"
#include "uid/uid.h"
#include "ml/ml.h"
#include "ring/ringlog.h"
#include "ring/ringlog.h"

/* ==========================================================================
 * Kernel data the function reads and writes
 * ========================================================================== */

uint32_t TIME_$CLOCKH;
uint32_t NODE_$ME;
uint8_t  sock_table_base[SOCK_TABLE_SIZE];
ringlog_ctl_t RINGLOG_$CTL;
MODULE_DATA_DEFINE(xns_$idp_data_t, XNS_IDP_$DATA, 0x00E2B314);

route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
uint32_t ROUTE_$PORT;
ml_$exclusion_t ROUTE_$SERVICE_MUTEX;

uint8_t  RIP_$HALT_PACKET[24];
uint16_t RTWIRED_$SEND_FLAGS;
MODULE_DATA_DEFINE(route_$rtwired_data_t, ROUTE_$RTWIRED_DATA, 0x00E87D80);

uint8_t  RINGLOG_$ROUTE_FORWARD[4] = { 0x00, 0x00, 0x20, 0x48 };
uint32_t RTWIRED_$CALLBACK_DATA;
MODULE_DATA_DEFINE(route_$unwired_data_t, ROUTE_$UNWIRED_DATA, 0x00E825DC);

/* ==========================================================================
 * Mock state
 * ========================================================================== */

#define MOCK_SOCK           7           /* ROUTE_$SOCK for every test */
#define MOCK_PKT_PAGE_SIZE  0x400

static sock_$sock_t mock_sock_desc;

/* One 1KB-aligned page so the "page + 0x3FC" physical-address read is valid */
static uint8_t mock_pkt_page[2 * MOCK_PKT_PAGE_SIZE];
static route_$internet_hdr_t *mock_pkt;

static route_$port_stats_t mock_port_stats;

/* EC_$WAIT script */
static int16_t mock_wait_script[8];
static int     mock_wait_len;
static int     mock_wait_pos;
static ec_$wait_ecs_t  mock_last_ecs;
static ec_$wait_vals_t mock_last_vals;

/* SOCK_$GET */
static sock_$pkt_info_t mock_rcv_template;
static int8_t mock_sock_get_result = (int8_t)0xFF;   /* true = got a packet */
static int    mock_sock_get_calls;

/* SOCK_$PUT */
static int8_t   mock_sock_put_result;
static int      mock_sock_put_calls;
static uint16_t mock_sock_put_socket;
static uint8_t  mock_sock_put_flags;

/* RIP_$FIND_NEXTHOP */
static status_$t     mock_nexthop_status;
static int16_t       mock_nexthop_port;
static rip_$nexthop_t mock_nexthop_value;
static int           mock_nexthop_calls;

/* MAC_OS */
static status_$t mock_arp_status;
static int       mock_arp_calls;
static int       mock_mac_send_calls;
static mac_os_$send_pkt_t mock_mac_send_rec;
static int16_t  *mock_mac_send_channel;

/* NET_IO */
static int      mock_net_io_calls;
static int16_t  mock_net_io_port;
static uint32_t mock_net_io_hdr_pa;
static uint16_t mock_net_io_hdr_len;
static uint16_t mock_net_io_data_len;
static uint32_t mock_net_io_pkt;
static int      mock_ml_lock_depth;

/* Buffer return */
static int      mock_rtn_hdr_calls;
static uint32_t mock_rtn_hdr_value;
static int      mock_dump_data_calls;
static int16_t  mock_dump_data_len;

/* Misc */
static int      mock_crash_calls;
static int      mock_broadcast_std_calls;
static int      mock_broadcast_n_calls;
static int      mock_ringlog_calls;
static int      mock_unwire_calls;
static uint32_t mock_unwire_order[ROUTE_$MAX_WIRED_PAGES];
static int      mock_sock_close_calls;
static uint16_t mock_sock_close_arg;
static int      mock_unbind_calls;
static int      mock_set_service_calls;
static int16_t  mock_last_service_op;
static int      mock_hop_and_sum_calls;

/* ==========================================================================
 * Mock implementations
 * ========================================================================== */

uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{
    (void)ecs; (void)wait_val; (void)num_ecs;
    return 0;
}

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    mock_last_ecs = ecs;
    mock_last_vals = vals;
    if (mock_wait_pos >= mock_wait_len) {
        return 2;   /* fall out through the shutdown path */
    }
    return mock_wait_script[mock_wait_pos++];
}

int8_t SOCK_$GET(uint16_t sock_num, void *pkt_info)
{
    (void)sock_num;
    mock_sock_get_calls++;
    *(sock_$pkt_info_t *)pkt_info = mock_rcv_template;
    return mock_sock_get_result;
}

int8_t SOCK_$PUT(uint16_t sock_num, sock_$pkt_info_t *pkt_info, int8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2)
{
    (void)pkt_info; (void)ec_param1; (void)ec_param2;
    mock_sock_put_calls++;
    mock_sock_put_socket = sock_num;
    mock_sock_put_flags = flags;
    return mock_sock_put_result;
}

void SOCK_$CLOSE(uint16_t sock_num)
{
    mock_sock_close_calls++;
    mock_sock_close_arg = sock_num;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    (void)status_p;
    mock_crash_calls++;
}

void NETWORK_$SET_SERVICE(int16_t *op_ptr, uint32_t *value_ptr,
                          status_$t *status_p)
{
    (void)value_ptr;
    mock_set_service_calls++;
    mock_last_service_op = *op_ptr;
    *status_p = status_$ok;
}

void PROC1_$SET_LOCK(uint16_t lock_id) { (void)lock_id; }
void PROC1_$CLR_LOCK(uint16_t lock_id) { (void)lock_id; }

void PROC1_$UNBIND(uint16_t pid, status_$t *status_ret)
{
    (void)pid;
    mock_unbind_calls++;
    *status_ret = status_$ok;
}

void RIP_$BROADCAST(boolean flags)
{
    if (flags < 0) {
        mock_broadcast_std_calls++;
    } else {
        mock_broadcast_n_calls++;
    }
}

int16_t RIP_$FIND_NEXTHOP(void *addr_info, boolean flags, int16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret)
{
    (void)addr_info; (void)flags;
    mock_nexthop_calls++;
    *port_ret = mock_nexthop_port;
    *(rip_$nexthop_t *)nexthop_ret = mock_nexthop_value;
    *status_ret = mock_nexthop_status;
    return 0;
}

int16_t XNS_IDP_$HOP_AND_SUM(uint16_t current_sum, int16_t hop_offset)
{
    (void)hop_offset;
    mock_hop_and_sum_calls++;
    return (int16_t)(current_sum + 1);
}

int16_t RINGLOG_$LOGIT(uint8_t *header_info, void *pkt_info)
{
    (void)header_info; (void)pkt_info;
    mock_ringlog_calls++;
    return 0;
}

void MAC_OS_$ARP(void *addr_info, int16_t port_num, uint16_t *mac_addr,
                 uint8_t *flags, status_$t *status_ret)
{
    (void)addr_info; (void)port_num; (void)mac_addr;
    mock_arp_calls++;
    if (flags != NULL) {
        *flags = 0;
    }
    *status_ret = mock_arp_status;
}

void MAC_OS_$SEND(int16_t *channel, mac_os_$send_pkt_t *pkt_desc,
                  int16_t *bytes_sent, status_$t *status_ret)
{
    mock_mac_send_calls++;
    mock_mac_send_channel = channel;
    mock_mac_send_rec = *pkt_desc;
    *bytes_sent = 0;
    *status_ret = status_$ok;
}

void ML_$LOCK(int16_t resource_id)   { (void)resource_id; mock_ml_lock_depth++; }
void ML_$UNLOCK(int16_t resource_id) { (void)resource_id; mock_ml_lock_depth--; }

void NET_IO_$SEND(int16_t port, uint32_t *hdr_ptr, uint32_t hdr_pa,
                  uint16_t hdr_len, uint32_t data_va, uint32_t *data_pages,
                  int16_t data_len, uint16_t flags,
                  net_io_$send_info_t *send_info, status_$t *status_ret)
{
    (void)data_va; (void)data_pages; (void)flags; (void)send_info;
    mock_net_io_calls++;
    mock_net_io_port = port;
    mock_net_io_pkt = hdr_ptr[0];
    mock_net_io_hdr_pa = hdr_pa;
    mock_net_io_hdr_len = hdr_len;
    mock_net_io_data_len = (uint16_t)data_len;
    *status_ret = status_$ok;
}

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    mock_rtn_hdr_calls++;
    mock_rtn_hdr_value = *va_ptr;
}

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    (void)buffers;
    mock_dump_data_calls++;
    mock_dump_data_len = len;
}

void WP_$UNWIRE(uint32_t wired_addr)
{
    if (mock_unwire_calls < ROUTE_$MAX_WIRED_PAGES) {
        mock_unwire_order[mock_unwire_calls] = wired_addr;
    }
    mock_unwire_calls++;
}

/* ==========================================================================
 * The translation unit under test
 *
 * route_$port_t.driver_stats is a 32-bit field, so on a 64-bit host it cannot
 * hold a real pointer.  Rebind the one dereference ROUTE_$PROCESS makes to
 * the mock block; the target definition is the plain cast.
 * ========================================================================== */

#undef ROUTE_$PORT_STATS
#define ROUTE_$PORT_STATS(port)   (&mock_port_stats)

#include "route/process.c"

/* ==========================================================================
 * Fixture helpers
 * ========================================================================== */

static void reset_mocks(void)
{
    memset(&mock_sock_desc, 0, sizeof(mock_sock_desc));
    memset(mock_pkt_page, 0, sizeof(mock_pkt_page));
    memset(&mock_port_stats, 0, sizeof(mock_port_stats));
    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    memset(ROUTE_$RTWIRED_DATA.q_depth, 0, sizeof(ROUTE_$RTWIRED_DATA.q_depth));
    memset(ROUTE_$RTWIRED_DATA.wired_pages, 0, sizeof(ROUTE_$RTWIRED_DATA.wired_pages));
    memset(&mock_rcv_template, 0, sizeof(mock_rcv_template));
    memset(&mock_mac_send_rec, 0, sizeof(mock_mac_send_rec));
    memset(mock_unwire_order, 0, sizeof(mock_unwire_order));

    /*
     * sock_$pkt_info_t.hdr is a 32-bit target VA, so the mock page is
     * addressed through ARCH_HOST_VA_BASE.  The base is put one page BELOW
     * the array so that every VA handed out is non-zero (ARCH_VA_TO_PTR(0)
     * is NULL by design).
     */
    ARCH_HOST_VA_BASE = (uintptr_t)mock_pkt_page - MOCK_PKT_PAGE_SIZE;

    /* The header buffer starts at a 1KB boundary inside the mock page. */
    mock_pkt = (route_$internet_hdr_t *)
        (void *)(((uintptr_t)mock_pkt_page + MOCK_PKT_PAGE_SIZE - 1) &
                 ~(uintptr_t)(MOCK_PKT_PAGE_SIZE - 1));

    ROUTE_$WIRED_DATA.sock = MOCK_SOCK;
    SOCK_$EVENT_COUNTERS[MOCK_SOCK - 1] = (ec_$eventcount_t *)&mock_sock_desc;

    ROUTE_$RTWIRED_DATA.std_dlen_err = 0;
    ROUTE_$RTWIRED_DATA.std_too_far = 0;
    ROUTE_$RTWIRED_DATA.std_misroute = 0;
    ROUTE_$RTWIRED_DATA.std_pkts_routed = 0;
    ROUTE_$RTWIRED_DATA.dlen_err = 0;
    ROUTE_$RTWIRED_DATA.too_far = 0;
    ROUTE_$RTWIRED_DATA.misroute = 0;
    ROUTE_$RTWIRED_DATA.pkts_routed = 0;
    ROUTE_$WIRED_DATA.sock_ecval = 0;
    ROUTE_$WIRED_DATA.control_ecval = 0;
    ROUTE_$WIRED_DATA.n_routing_ports = 0;
    ROUTE_$WIRED_DATA.std_n_routing_ports = 0;
    ROUTE_$RTWIRED_DATA.n_user_ports = 1;    /* keep the unwire loop out of the way */
    ROUTE_$RTWIRED_DATA.n_wired_pages = 0;
    ROUTE_$WIRED_DATA.routing = 0;
    ROUTE_$RTWIRED_DATA.fwd_timeout = 1;
    ROUTE_$RTWIRED_DATA.pid = 0x1234;
    TIME_$CLOCKH = 1000;
    NODE_$ME = 0xABCDE;
    RING_$LOGGING_NOW = 0;

    mock_wait_len = 0;
    mock_wait_pos = 0;
    mock_sock_get_result = (int8_t)0xFF;
    mock_sock_get_calls = 0;
    mock_sock_put_result = (int8_t)0xFF;
    mock_sock_put_calls = 0;
    mock_sock_put_socket = 0;
    mock_sock_put_flags = 0xAA;
    mock_nexthop_status = status_$ok;
    mock_nexthop_port = 1;
    mock_nexthop_value.network = 0x11;
    mock_nexthop_value.host_hi = 0;
    mock_nexthop_value.host_lo = 0x000FFFFF;
    mock_nexthop_calls = 0;
    mock_arp_status = status_$ok;
    mock_arp_calls = 0;
    mock_mac_send_calls = 0;
    mock_mac_send_channel = NULL;
    mock_net_io_calls = 0;
    mock_ml_lock_depth = 0;
    mock_rtn_hdr_calls = 0;
    mock_rtn_hdr_value = 0;
    mock_dump_data_calls = 0;
    mock_dump_data_len = 0;
    mock_crash_calls = 0;
    mock_broadcast_std_calls = 0;
    mock_broadcast_n_calls = 0;
    mock_ringlog_calls = 0;
    mock_unwire_calls = 0;
    mock_sock_close_calls = 0;
    mock_unbind_calls = 0;
    mock_set_service_calls = 0;
    mock_hop_and_sum_calls = 0;
}

/* Script: one socket wakeup, then the control wakeup that ends the loop. */
static void script_one_packet(void)
{
    mock_wait_script[0] = 1;
    mock_wait_script[1] = 2;
    mock_wait_len = 2;
}

/*
 * Build the SOCK_$GET record for a packet.  `std` selects XNS ("standard")
 * routing, which also moves the IDP header to the packet base.
 */
static xns_$idp_header_t *setup_packet(int std, uint8_t routing_type,
                                       uint16_t data_len, uint16_t hdr_len,
                                       uint32_t data_page0)
{
    mock_rcv_template.hdr = ARCH_PTR_TO_VA(mock_pkt);
    mock_rcv_template.flags = std ? 0x0002 : 0x0000;
    mock_rcv_template.data_len = data_len;
    mock_rcv_template.hdr_len = hdr_len;
    mock_rcv_template.data_pages[0] = data_page0;

    mock_pkt->routing_type = routing_type;
    mock_pkt->hdr_len = hdr_len;
    mock_pkt->data_len = data_len;

    return std ? (xns_$idp_header_t *)mock_pkt : &mock_pkt->idp;
}

/* Make ROUTE_$PORT_ARRAY[idx] a usable destination port. */
static void setup_port(int idx, uint16_t active, uint16_t port_type,
                       uint16_t socket)
{
    ROUTE_$PORT_ARRAY[idx].active = active;
    ROUTE_$PORT_ARRAY[idx].port_type = port_type;
    ROUTE_$PORT_ARRAY[idx].socket = socket;
    ROUTE_$PORT_ARRAY[idx].driver_stats = (uint32_t)(uintptr_t)&mock_port_stats;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * Start-up sequence: routing is marked active, the service is switched on
 * with opcode 0, and the first broadcast deadline is TIME_$CLOCKH.
 * 0x00E8742E - 0x00E8745E.
 */
TEST(startup_and_shutdown)
{
    ROUTE_$PROCESS();

    ASSERT_EQ(2, mock_set_service_calls);
    /* 0x00E8789E, the "and not" opcode cell: the file static in
     * route/process.c, visible because the test includes that file. */
    ASSERT_EQ(net_service_and_not_bits, mock_last_service_op);
    ASSERT_EQ(0, ROUTE_$WIRED_DATA.routing);                              /* cleared on exit */
    ASSERT_EQ(0, ROUTE_$UNWIRED_DATA.start_time);
    ASSERT_EQ(0xFFFF, ROUTE_$WIRED_DATA.sock);
    ASSERT_EQ(1, mock_sock_close_calls);
    ASSERT_EQ(MOCK_SOCK, mock_sock_close_arg);
    ASSERT_EQ(1, mock_unbind_calls);
    /* One advance before the loop plus one on the way out (0xE87414/0xE877FE) */
    ASSERT_EQ(2, ROUTE_$WIRED_DATA.control_ecval);
}

/*
 * Timer wakeup: broadcast only while more than one routing port of that kind
 * exists, and re-arm at TIME_$CLOCKH + 0x72.  0x00E877C2 - 0x00E877F6.
 */
TEST(timer_broadcasts)
{
    ROUTE_$WIRED_DATA.n_routing_ports = 2;
    ROUTE_$WIRED_DATA.std_n_routing_ports = 1;
    mock_wait_script[0] = 0;
    mock_wait_script[1] = 2;
    mock_wait_len = 2;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_broadcast_n_calls);
    ASSERT_EQ(0, mock_broadcast_std_calls);
    /* The rearmed deadline is what the second (shutdown) wait was given. */
    ASSERT_EQ(TIME_$CLOCKH + ROUTE_$BROADCAST_INTERVAL, mock_last_vals.val[0]);
}

TEST(timer_broadcasts_both_modes)
{
    ROUTE_$WIRED_DATA.n_routing_ports = 2;
    ROUTE_$WIRED_DATA.std_n_routing_ports = 5;
    mock_wait_script[0] = 0;
    mock_wait_script[1] = 2;
    mock_wait_len = 2;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_broadcast_n_calls);
    ASSERT_EQ(1, mock_broadcast_std_calls);
}

/* The three event counts and their values, 0x00E87460 - 0x00E87482. */
TEST(wait_arrays)
{
    ROUTE_$WIRED_DATA.sock_ecval = 0x40;
    ROUTE_$WIRED_DATA.control_ecval = 0x50;

    ROUTE_$PROCESS();

    ASSERT_TRUE(mock_last_ecs.ec[0] == (ec_$eventcount_t *)&TIME_$CLOCKH);
    ASSERT_TRUE(mock_last_ecs.ec[1] == (ec_$eventcount_t *)&mock_sock_desc);
    ASSERT_TRUE(mock_last_ecs.ec[2] == &ROUTE_$WIRED_DATA.control_ec);
    ASSERT_EQ(1000, mock_last_vals.val[0]);
    ASSERT_EQ(0x40, mock_last_vals.val[1]);
    /* The pre-loop advance bumped the control value to 0x51 */
    ASSERT_EQ(0x51, mock_last_vals.val[2]);
}

/* A false SOCK_$GET on a socket wakeup is fatal - 0x00E874B8. */
TEST(sock_get_failure_crashes)
{
    script_one_packet();
    mock_sock_get_result = 0;
    setup_packet(0, 2, 0, 0x20, 0);

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_crash_calls);
}

/* Packet-size statistics bucket by the socket queue depth, capped at 0x80. */
TEST(packet_stats_bucket)
{
    script_one_packet();
    setup_packet(0, 1, 0, 0x20, 0);     /* routing type 1: not forwarded */
    mock_sock_desc.queue_count = 5;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.q_depth[5]);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.q_depth[0]);
}

TEST(packet_stats_bucket_capped)
{
    script_one_packet();
    setup_packet(0, 1, 0, 0x20, 0);
    mock_sock_desc.queue_count = 0xF0;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.q_depth[0x80]);
}

/*
 * Routing type 1 with no standard-routing flag means should_forward is false,
 * so no route lookup happens and the buffers come back.  0x00E874F4.
 */
TEST(routing_type_below_two_is_not_forwarded)
{
    script_one_packet();
    setup_packet(0, 1, 0, 0x20, 0);

    ROUTE_$PROCESS();

    ASSERT_EQ(0, mock_nexthop_calls);
    ASSERT_EQ(1, mock_rtn_hdr_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(mock_pkt), mock_rtn_hdr_value);
    ASSERT_EQ(1, mock_dump_data_calls);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.pkts_routed);
    ASSERT_EQ(1, ROUTE_$WIRED_DATA.sock_ecval);
}

/*
 * The standard-routing flag alone makes a packet a forwarding candidate even
 * when the internet header's routing type is below 2 - the "or.b D2b,D3b" at
 * 0x00E874FE.
 */
TEST(std_flag_forces_forward_candidate)
{
    script_one_packet();
    setup_packet(1, 0, 0x40, 0x20, 0);
    setup_port(1, 4, 1, 0);             /* bit 4 -> allowed for std routing */

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_nexthop_calls);
    ASSERT_EQ(1, mock_arp_calls);
    ASSERT_EQ(1, mock_mac_send_calls);
    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.std_pkts_routed);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.pkts_routed);
}

/* The IDP header base differs between the two routing modes - 0x00E87500. */
TEST(idp_header_base_selection)
{
    xns_$idp_header_t *idp;

    /* non-standard: header at packet + 0x28 */
    script_one_packet();
    idp = setup_packet(0, 2, 0x40, 0x20, 0x2000);
    idp->checksum = 0xFFFF;             /* skip the checksum recompute */
    idp->transport_ctl = 3;
    setup_port(1, 3, 1, 0);
    mock_nexthop_status = 0x3C0001;     /* stop after the lookup */

    ROUTE_$PROCESS();

    ASSERT_EQ(4, mock_pkt->idp.transport_ctl);
    ASSERT_EQ(0, ((xns_$idp_header_t *)mock_pkt)->transport_ctl);

    /* standard: header at the packet base */
    reset_mocks();
    script_one_packet();
    idp = setup_packet(1, 0, 0x40, 0x20, 0x2000);
    idp->checksum = 0xFFFF;
    idp->transport_ctl = 3;
    mock_nexthop_status = 0x3C0001;

    ROUTE_$PROCESS();

    ASSERT_EQ(4, ((xns_$idp_header_t *)mock_pkt)->transport_ctl);
}

/* The checksum is only recomputed when it is not 0xFFFF - 0x00E8752A. */
TEST(checksum_recompute)
{
    xns_$idp_header_t *idp;

    script_one_packet();
    idp = setup_packet(0, 2, 0x40, 0x20, 0x2000);
    idp->checksum = 0x1234;
    mock_nexthop_status = 0x3C0001;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_hop_and_sum_calls);
    ASSERT_EQ(0x1235, idp->checksum);
}

/*
 * A non-standard packet claiming a payload but carrying no data pages is
 * dropped against 0xE87FBC before the hop check - 0x00E8750C.
 */
TEST(drop_when_no_data_pages)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0 /* no data page */);

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.dlen_err);
    ASSERT_EQ(0, mock_nexthop_calls);
    ASSERT_EQ(1, mock_rtn_hdr_calls);
}

/* The same shape with a data page present is not dropped. */
TEST(no_drop_when_data_pages_present)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 3, 1, 0);

    ROUTE_$PROCESS();

    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.dlen_err);
    ASSERT_EQ(1, mock_nexthop_calls);
}

/*
 * The hop limit fires at transport_ctl >= 0x10 after the increment, i.e. an
 * incoming 0x0F is the last value that still routes - 0x00E8754A.
 */
TEST(hop_limit_boundary)
{
    xns_$idp_header_t *idp;

    script_one_packet();
    idp = setup_packet(0, 2, 0x40, 0x20, 0x2000);
    idp->checksum = 0xFFFF;
    idp->transport_ctl = 0x0E;          /* becomes 0x0F: still routable */
    setup_port(1, 3, 1, 0);

    ROUTE_$PROCESS();

    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.too_far);
    ASSERT_EQ(1, mock_nexthop_calls);

    reset_mocks();
    script_one_packet();
    idp = setup_packet(0, 2, 0x40, 0x20, 0x2000);
    idp->checksum = 0xFFFF;
    idp->transport_ctl = 0x0F;          /* becomes 0x10: dropped */
    setup_port(1, 3, 1, 0);

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.too_far);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.std_too_far);
    ASSERT_EQ(0, mock_nexthop_calls);
}

TEST(hop_limit_counts_against_std_bucket)
{
    xns_$idp_header_t *idp;

    script_one_packet();
    idp = setup_packet(1, 0, 0x40, 0x20, 0x2000);
    idp->checksum = 0xFFFF;
    idp->transport_ctl = 0x20;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.std_too_far);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.too_far);
}

/* A failed route lookup counts in the per-mode "dropped route" bucket. */
TEST(no_route_counts_dropped)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    mock_nexthop_status = 0x3C0001;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.misroute);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.pkts_routed);
    ASSERT_EQ(1, mock_rtn_hdr_calls);
}

/*
 * The destination port's "active" word is tested against 0x28 for normal
 * routing and 0x30 for standard routing - 0x00E875C4 / 0x00E875D6.
 */
TEST(port_active_mask_normal)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 4, 1, 0);             /* bit 4: in 0x30, NOT in 0x28 */

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.misroute);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.pkts_routed);
}

TEST(port_active_mask_std)
{
    script_one_packet();
    setup_packet(1, 0, 0x40, 0x20, 0x2000);
    setup_port(1, 3, 1, 0);             /* bit 3: in 0x28, NOT in 0x30 */

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.std_misroute);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.std_pkts_routed);
}

/*
 * Normal routing rewrites the internet header's source node and destination
 * node regardless of the port check - 0x00E875E6.
 */
TEST(header_rewrite_for_normal_routing)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 3, 1, 0);
    mock_nexthop_value.host_lo = 0xFFF12345;

    ROUTE_$PROCESS();

    ASSERT_EQ(0xABCDE, mock_pkt->src_node);
    ASSERT_EQ(0x12345, mock_pkt->dest_node);
}

/* Standard routing leaves the internet header alone. */
TEST(no_header_rewrite_for_std_routing)
{
    script_one_packet();
    setup_packet(1, 0, 0x40, 0x20, 0x2000);
    setup_port(1, 4, 1, 0);
    mock_pkt->src_node = 0x5555;

    ROUTE_$PROCESS();

    ASSERT_EQ(0x5555, mock_pkt->src_node);
}

/*
 * A type 2 (user routing) destination goes out through SOCK_$PUT, the packet
 * is considered handed off, and the buffers are NOT returned - 0x00E87618.
 */
TEST(user_routing_port_uses_sock_put)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 3, ROUTE_PORT_TYPE_ROUTING, 0x0055);
    mock_sock_desc.queue_count = 4;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_sock_put_calls);
    ASSERT_EQ(0x0055, mock_sock_put_socket);
    ASSERT_EQ(0, mock_sock_put_flags);          /* was_forwarded, still false */
    ASSERT_EQ(1, mock_port_stats.queue_depth[4]);
    ASSERT_EQ(0, mock_port_stats.failed_puts);
    ASSERT_EQ(1, ROUTE_$PORT_ARRAY[1].forward_count);
    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.pkts_routed);
    ASSERT_EQ(0, mock_rtn_hdr_calls);           /* the socket owns it now */
    ASSERT_EQ(0, mock_dump_data_calls);
}

/* Deep queues land in the overflow counter at +0x02 instead - 0x00E8764E. */
TEST(user_routing_port_deep_queue_bucket)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 3, ROUTE_PORT_TYPE_ROUTING, 0x0055);
    mock_sock_desc.queue_count = 0x21;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_port_stats.deep_queue_puts);
    ASSERT_EQ(0, mock_port_stats.queue_depth[0x21 & 0x1F]);
}

/* The 0x20 boundary itself still buckets. */
TEST(user_routing_port_queue_bucket_boundary)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 3, ROUTE_PORT_TYPE_ROUTING, 0x0055);
    mock_sock_desc.queue_count = 0x20;

    ROUTE_$PROCESS();

    ASSERT_EQ(0, mock_port_stats.deep_queue_puts);
    ASSERT_EQ(1, mock_port_stats.queue_depth[0x20]);
}

/*
 * A failed SOCK_$PUT bumps the failure counter, leaves was_forwarded false so
 * the buffers come back, but still counts as a forward - 0x00E87660.
 */
TEST(user_routing_port_put_failure)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 3, ROUTE_PORT_TYPE_ROUTING, 0x0055);
    mock_sock_put_result = 0;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_port_stats.failed_puts);
    ASSERT_EQ(1, ROUTE_$PORT_ARRAY[1].forward_count);
    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.pkts_routed);
    ASSERT_EQ(1, mock_rtn_hdr_calls);
    ASSERT_EQ(1, mock_dump_data_calls);
}

/* Ring logging is only invoked while RING_$LOGGING_NOW is true - 0x00E87602. */
TEST(ringlog_gated_on_logging_flag)
{
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 3, ROUTE_PORT_TYPE_ROUTING, 0x0055);

    ROUTE_$PROCESS();
    ASSERT_EQ(0, mock_ringlog_calls);

    reset_mocks();
    script_one_packet();
    setup_packet(0, 2, 0x40, 0x20, 0x2000);
    setup_port(1, 3, ROUTE_PORT_TYPE_ROUTING, 0x0055);
    RING_$LOGGING_NOW = (int8_t)0xFF;

    ROUTE_$PROCESS();
    ASSERT_EQ(1, mock_ringlog_calls);
}

/* Anything over 0x400 bytes cannot go out on a real port - 0x00E8766C. */
TEST(oversize_packet_counted)
{
    script_one_packet();
    setup_packet(0, 2, 0x401, 0x20, 0x2000);
    setup_port(1, 3, 1, 0);

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.dlen_err);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.pkts_routed);
    ASSERT_EQ(0, mock_net_io_calls);
    ASSERT_EQ(1, mock_rtn_hdr_calls);

    reset_mocks();
    script_one_packet();
    setup_packet(1, 0, 0x401, 0x20, 0x2000);
    setup_port(1, 4, 1, 0);

    ROUTE_$PROCESS();

    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.std_dlen_err);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.std_pkts_routed);
}

/* Exactly 0x400 still goes out. */
TEST(max_size_packet_still_sent)
{
    script_one_packet();
    setup_packet(0, 2, 0x400, 0x20, 0x2000);
    setup_port(1, 3, 1, 0);

    ROUTE_$PROCESS();

    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.dlen_err);
    ASSERT_EQ(1, mock_net_io_calls);
    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.pkts_routed);
}

/*
 * The NET_IO_$SEND path: the header physical address comes from the last
 * longword of the header's 1KB page, the lock is balanced, and the lengths
 * are taken from the internet header - 0x00E8770A.
 */
TEST(net_io_send_arguments)
{
    script_one_packet();
    setup_packet(0, 2, 0x123, 0x2A, 0x2000);
    setup_port(1, 3, 1, 0);
    mock_nexthop_port = 3;
    *(uint32_t *)((uint8_t *)mock_pkt + 0x3FC) = 0xDEADBEEF;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_net_io_calls);
    ASSERT_EQ(3, mock_net_io_port);
    ASSERT_EQ(ARCH_PTR_TO_VA(mock_pkt), mock_net_io_pkt);
    ASSERT_EQ(0xDEADBEEF, mock_net_io_hdr_pa);
    ASSERT_EQ(0x2A, mock_net_io_hdr_len);
    ASSERT_EQ(0x123, mock_net_io_data_len);
    ASSERT_EQ(0, mock_ml_lock_depth);
    /* The buffers still belong to us afterwards */
    ASSERT_EQ(1, mock_rtn_hdr_calls);
    ASSERT_EQ(0x123, mock_dump_data_len);
}

/*
 * The standard-routing send builds the 0x4C-byte MAC descriptor and passes
 * the port's IDP MAC channel - 0x00E876A4 - 0x00E876FE.
 */
TEST(mac_send_descriptor)
{
    script_one_packet();
    setup_packet(1, 0, 0x111, 0x22, 0x2000);
    mock_rcv_template.data_pages[1] = 0x3000;
    mock_nexthop_port = 2;
    setup_port(2, 4, 1, 0);             /* bit 4 -> allowed for std routing */

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_arp_calls);
    ASSERT_EQ(1, mock_mac_send_calls);
    ASSERT_EQ(0x22, mock_mac_send_rec.hdr_desc.length);
    ASSERT_EQ((uint32_t)(uintptr_t)mock_pkt, mock_mac_send_rec.hdr_desc.address);
    ASSERT_EQ(0, mock_mac_send_rec.hdr_desc.next);
    ASSERT_TRUE(mock_mac_send_rec.hdr_prebuilt < 0);
    ASSERT_EQ(ROUTE_$MAC_FRAME_TYPE, mock_mac_send_rec.frame_type);
    ASSERT_EQ(0x111, mock_mac_send_rec.data_length);
    ASSERT_EQ(0x2000, mock_mac_send_rec.data_pages[0]);
    ASSERT_EQ(0x3000, mock_mac_send_rec.data_pages[1]);
    ASSERT_TRUE(mock_mac_send_channel == XNS_IDP_$PORT_MAC_CHANNEL(2));
    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.std_pkts_routed);
}

/* An ARP failure clears should_forward, so nothing is counted - 0x00E876A0. */
TEST(arp_failure_stops_forward)
{
    script_one_packet();
    setup_packet(1, 0, 0x111, 0x22, 0x2000);
    setup_port(1, 4, 1, 0);
    mock_arp_status = 0x3A0013;

    ROUTE_$PROCESS();

    ASSERT_EQ(1, mock_arp_calls);
    ASSERT_EQ(0, mock_mac_send_calls);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.std_pkts_routed);
    ASSERT_EQ(1, mock_rtn_hdr_calls);
}

/*
 * The shutdown unwire loop walks the wired page array upwards from index 0
 * and only when no user ports remain - 0x00E87856.
 */
TEST(shutdown_unwire_order)
{
    ROUTE_$RTWIRED_DATA.n_user_ports = 0;
    ROUTE_$RTWIRED_DATA.n_wired_pages = 3;
    ROUTE_$RTWIRED_DATA.wired_pages[0] = 0x1000;
    ROUTE_$RTWIRED_DATA.wired_pages[1] = 0x2000;
    ROUTE_$RTWIRED_DATA.wired_pages[2] = 0x3000;

    ROUTE_$PROCESS();

    ASSERT_EQ(3, mock_unwire_calls);
    ASSERT_EQ(0x1000, mock_unwire_order[0]);
    ASSERT_EQ(0x2000, mock_unwire_order[1]);
    ASSERT_EQ(0x3000, mock_unwire_order[2]);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.n_wired_pages);
}

TEST(shutdown_keeps_wired_pages_when_user_ports_remain)
{
    ROUTE_$RTWIRED_DATA.n_user_ports = 1;
    ROUTE_$RTWIRED_DATA.n_wired_pages = 3;
    ROUTE_$RTWIRED_DATA.wired_pages[0] = 0x1000;

    ROUTE_$PROCESS();

    ASSERT_EQ(0, mock_unwire_calls);
    ASSERT_EQ(3, ROUTE_$RTWIRED_DATA.n_wired_pages);
}

/* ==========================================================================
 * Layout checks that would otherwise only fire on the m68k build
 * ========================================================================== */

TEST(record_layouts)
{
    /*
     * sock_$pkt_info_t and sock_$sock_t both start with a pointer, so their
     * byte offsets only hold where a pointer is 4 bytes wide; those layouts
     * are pinned by the _Static_asserts in sock/sock.h under ARCH_M68K.  The
     * records below are made of fixed-width fields (and are packed where the
     * m68k alignment differs), so they must lay out identically everywhere.
     */
    ASSERT_EQ(0x4C, sizeof(mac_os_$send_pkt_t));
    ASSERT_EQ(0x18, offsetof(mac_os_$send_pkt_t, is_broadcast));
    ASSERT_EQ(0x1C, offsetof(mac_os_$send_pkt_t, hdr_desc));
    ASSERT_EQ(0x28, offsetof(mac_os_$send_pkt_t, hdr_prebuilt));
    ASSERT_EQ(0x30, offsetof(mac_os_$send_pkt_t, frame_type));
    ASSERT_EQ(0x38, offsetof(mac_os_$send_pkt_t, data_length));
    ASSERT_EQ(0x3C, offsetof(mac_os_$send_pkt_t, data_pages));
    ASSERT_EQ(0x8E, sizeof(route_$port_stats_t));
    ASSERT_EQ(0x0A, offsetof(route_$port_stats_t, queue_depth));
    ASSERT_EQ(10, sizeof(rip_$nexthop_t));
    ASSERT_EQ(6, offsetof(rip_$nexthop_t, host_lo));
    ASSERT_EQ(0x5C, sizeof(route_$port_t));
}

/* ========================================================================== */

int main(void)
{
    printf("ROUTE_$PROCESS tests\n");

    RUN_TEST(startup_and_shutdown);
    RUN_TEST(timer_broadcasts);
    RUN_TEST(timer_broadcasts_both_modes);
    RUN_TEST(wait_arrays);
    RUN_TEST(sock_get_failure_crashes);
    RUN_TEST(packet_stats_bucket);
    RUN_TEST(packet_stats_bucket_capped);
    RUN_TEST(routing_type_below_two_is_not_forwarded);
    RUN_TEST(std_flag_forces_forward_candidate);
    RUN_TEST(idp_header_base_selection);
    RUN_TEST(checksum_recompute);
    RUN_TEST(drop_when_no_data_pages);
    RUN_TEST(no_drop_when_data_pages_present);
    RUN_TEST(hop_limit_boundary);
    RUN_TEST(hop_limit_counts_against_std_bucket);
    RUN_TEST(no_route_counts_dropped);
    RUN_TEST(port_active_mask_normal);
    RUN_TEST(port_active_mask_std);
    RUN_TEST(header_rewrite_for_normal_routing);
    RUN_TEST(no_header_rewrite_for_std_routing);
    RUN_TEST(user_routing_port_uses_sock_put);
    RUN_TEST(user_routing_port_deep_queue_bucket);
    RUN_TEST(user_routing_port_queue_bucket_boundary);
    RUN_TEST(user_routing_port_put_failure);
    RUN_TEST(ringlog_gated_on_logging_flag);
    RUN_TEST(oversize_packet_counted);
    RUN_TEST(max_size_packet_still_sent);
    RUN_TEST(net_io_send_arguments);
    RUN_TEST(mac_send_descriptor);
    RUN_TEST(arp_failure_stops_forward);
    RUN_TEST(shutdown_unwire_order);
    RUN_TEST(shutdown_keeps_wired_pages_when_user_ports_remain);
    RUN_TEST(record_layouts);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
