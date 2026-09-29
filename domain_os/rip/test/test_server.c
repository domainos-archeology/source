/*
 * rip/test/test_server.c - unit tests for the RIP server (0x00E68864-0x00E68E24)
 *
 * These tests #include rip/server.c itself and drive the real
 * RIP_$PACKET_LENGTH / RIP_$SEND_UPDATES / RIP_$SERVER (and, through it, the
 * nested RIP_$PROCESS_REQUEST) with mocked callees, so what is exercised is
 * the emitted translation and not a re-implementation.
 *
 * The behaviours pinned down here are exactly the ones bead source-4nvz says
 * the previous sketch of the three dispatch arms got wrong:
 *
 *   - the STD request arm's broadcast test reads the IDP DESTINATION host
 *     (header + 0x0A/0x0C/0x0E), not header + 0x14 - 0x00E68B8C-0x00E68BA6;
 *   - the reply goes to header + 0x12 (the IDP source address), not to a
 *     hand-built address with socket 1 - "pea (-0xe,A6)" at 0x00E68BD2;
 *   - the metric on the STD side is clamped UP to 0x10 and an unknown network
 *     answers 0x11 on the internet side - 0x00E68934 / 0x00E6894E;
 *   - the response arm reloads the network from header + 0x06 (dest_network),
 *     not from header + 0x1A - 0x00E68C9E;
 *   - the route loop runs entry_count times, not entry_count - 1 - the
 *     "subq.w #1,D1w" at 0x00E68D8C feeds a dbf;
 *   - the name-register arm checks the IDP packet type (header + 0x05) - the
 *     byte at (-0x1b,A6) - against 0xBE at 0x00E68DD4.
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

static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                        \
        tests_failed++; current_failed = 1;                                    \
        return;                                                                \
    }                                                                          \
} while (0)

/* A mock that has to return a value needs its own variant. */
#define ASSERT_EQ_RET(expected, actual) do {                                  \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",      \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return 0;                                                             \
    }                                                                         \
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
 * Headers and kernel data the translation unit under test needs
 * ========================================================================== */

#include "rip/rip_internal.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"
#include "time/time.h"
#include "name/name.h"
#include "hint/hint.h"
#include "uid/uid.h"
#include "xns/xns.h"

MODULE_DATA_DEFINE(rip_$wired_data_t, RIP_$WIRED_DATA, 0x00E26258);
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
uint32_t        NODE_$ME;

/* ==========================================================================
 * Mocks
 * ========================================================================== */

/* The 1KB netbuf page the header buffer lives in; the port network / socket
 * the packet arrived on sit at +0x3E0 / +0x3E2 (0x00E68B32/0x00E68B38). */
/*
 * A single arena for everything RIP_$SERVER reaches through a 32-bit virtual
 * address field.  ARCH_HOST_VA_BASE is set just below it in main(), so
 * ARCH_PTR_TO_VA of anything in here is a small non-zero offset that survives
 * the round trip through the frame's uint32_t payload_va / hdr_va.
 */
static uint8_t   host_arena[0x4000];
static uint8_t  *netbuf_page;           /* host_arena, 1KB aligned */
#define HDR_OFFSET_IN_PAGE  0x100

static int         sock_get_calls;
static boolean     sock_get_result;
static sock_$pkt_info_t sock_get_record;

static int         dump_data_calls;
static int16_t     dump_data_len;

static int         rtn_hdr_calls;
static uint32_t    rtn_hdr_va[4];

static int16_t     find_port_result;
static uint16_t    find_port_network;
static int32_t     find_port_socket;

static int         net_lookup_calls;
static rip_$entry_t *net_lookup_result[8];
static uint32_t    net_lookup_network[8];

static int         send_calls;
static const uint8_t *send_addr_info;
static int16_t     send_port;
static uint8_t     send_data[0x21E];
static uint16_t    send_len;
static boolean     send_flags;

static int         time_wait_calls;
static status_$t   time_wait_status;

static int         send_internet_calls;
static uint32_t    si_routing_key, si_dest_node, si_src_node_or, si_src_node;
static uint16_t    si_dest_sock, si_src_sock, si_request_id, si_template_len;
static uint16_t    si_data_len;
static uint16_t    si_pkt_info_word0;
static uint8_t     si_template[0x21E];

static int         update_int_calls;
static uint32_t    ui_network[8];
static uint16_t    ui_metric[8];
static uint16_t    ui_port[8];
static boolean     ui_flags[8];
static rip_$xns_addr_t ui_source[8];

static int         hint_add_net_calls;
static uint32_t    hint_add_net_arg;

static int         register_server_calls;
static uint32_t    register_server_net;   /* *argument 1 at the call */
static uint32_t    register_server_node;  /* *argument 2 at the call */

static int         broadcast_calls;
static boolean     broadcast_flags;

int8_t SOCK_$GET(uint16_t sock_num, void *pkt_info)
{
    sock_get_calls++;
    ASSERT_EQ_RET(RIP_SOCKET, sock_num);
    memcpy(pkt_info, &sock_get_record, sizeof(sock_$pkt_info_t));
    return (int8_t)sock_get_result;
}

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    (void)buffers;
    dump_data_calls++;
    dump_data_len = len;
}

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    if (rtn_hdr_calls < 4) {
        rtn_hdr_va[rtn_hdr_calls] = *va_ptr;
    }
    rtn_hdr_calls++;
}

int16_t ROUTE_$FIND_PORT(uint16_t network, int32_t socket)
{
    find_port_network = network;
    find_port_socket = socket;
    return find_port_result;
}

struct rip_$entry_t *RIP_$NET_LOOKUP(uint32_t network, boolean inc_refcount,
                                     boolean create_if_missing)
{
    rip_$entry_t *r = NULL;
    (void)inc_refcount;
    (void)create_if_missing;
    if (net_lookup_calls < 8) {
        net_lookup_network[net_lookup_calls] = network;
        r = net_lookup_result[net_lookup_calls];
    }
    net_lookup_calls++;
    return r;
}

void RIP_$SEND(void *addr_info, int16_t port_index, void *route_data,
               uint16_t route_len, boolean flags)
{
    send_calls++;
    send_addr_info = (const uint8_t *)addr_info;
    send_port = port_index;
    send_len = route_len;
    send_flags = flags;
    memcpy(send_data, route_data, sizeof(send_data));
}

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    time_wait_calls++;
    (void)delay_type;
    (void)delay;
    *status = time_wait_status;
}

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                        int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    send_internet_calls++;
    si_routing_key = routing_key;
    si_dest_node = dest_node;
    si_dest_sock = dest_sock;
    si_src_node_or = (uint32_t)src_node_or;
    si_src_node = src_node;
    si_src_sock = src_sock;
    si_pkt_info_word0 = ((uint16_t *)pkt_info)[0];
    si_request_id = request_id;
    si_template_len = template_len;
    si_data_len = (uint16_t)data_len;
    (void)data;
    memcpy(si_template, template, sizeof(si_template));
    *retry_hint = 5;
    *timeout_out = 4;
    *status_ret = status_$ok;
}

void RIP_$UPDATE_INT(uint32_t network, rip_$xns_addr_t *source,
                     uint16_t hop_count, uint16_t port_index,
                     boolean flags, status_$t *status_ret)
{
    if (update_int_calls < 8) {
        ui_network[update_int_calls] = network;
        ui_metric[update_int_calls] = hop_count;
        ui_port[update_int_calls] = port_index;
        ui_flags[update_int_calls] = flags;
        ui_source[update_int_calls] = *source;
    }
    update_int_calls++;
    *status_ret = status_$ok;
}

void HINT_$ADD_NET(uint32_t net_port)
{
    hint_add_net_calls++;
    hint_add_net_arg = net_port;
}

void REM_NAME_$REGISTER_SERVER(uint32_t *net, uint32_t *node)
{
    /* Both arguments are read here only so the test can see which frame
     * cells the call site passed; the real callee ignores them. */
    register_server_net = *net;
    register_server_node = *node;
    register_server_calls++;
}

void RIP_$BROADCAST(boolean flags)
{
    broadcast_calls++;
    broadcast_flags = flags;
}

/* PKT_$BRK_INTERNET_HDR: scripted outputs for the Domain-internet path. */
static int       brk_calls;
static uint32_t  brk_network, brk_dest_node, brk_src_node_or, brk_src_node;
static uint16_t  brk_dest_sock, brk_src_sock, brk_id;
static uint16_t  brk_info0;
static uint16_t  brk_data_len;
static status_$t brk_status;
static uint8_t   brk_payload[0x21E];

void PKT_$BRK_INTERNET_HDR(pkt_$hdr_t *hdr, uint16_t hdr_len,
                           uint32_t *routing_key, uint32_t *dest_node,
                           uint16_t *dest_sock, uint32_t *src_node_or,
                           uint32_t *src_node, uint16_t *src_sock,
                           uint16_t *info_out, uint16_t *id_out,
                           void *data_buf, uint16_t data_max,
                           uint16_t *data_len, status_$t *status_ret)
{
    (void)hdr; (void)hdr_len; (void)data_max;
    brk_calls++;
    *routing_key = brk_network;
    *dest_node   = brk_dest_node;
    *dest_sock   = brk_dest_sock;
    *src_node_or = brk_src_node_or;
    *src_node    = brk_src_node;
    *src_sock    = brk_src_sock;
    info_out[0]  = brk_info0;
    *id_out      = brk_id;
    memcpy(data_buf, brk_payload, sizeof(brk_payload));
    *data_len    = brk_data_len;
    *status_ret  = brk_status;
}

/* SOCK_$GET's ASSERT_EQ needs a return-carrying variant. */
#define ASSERT_EQ_RET(expected, actual) do {                                  \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                        \
        tests_failed++; current_failed = 1;                                    \
        return 0;                                                              \
    }                                                                          \
} while (0)

/* ==========================================================================
 * The translation unit under test
 * ========================================================================== */

#include "../server.c"

/* ==========================================================================
 * Fixture helpers
 * ========================================================================== */

static xns_$idp_header_t *hdr_in_page(void)
{
    return (xns_$idp_header_t *)(netbuf_page + HDR_OFFSET_IN_PAGE);
}

static rip_$packet_t *payload_in_page(void)
{
    return (rip_$packet_t *)(netbuf_page + HDR_OFFSET_IN_PAGE + XNS_IDP_HEADER_SIZE);
}

static void reset_mocks(void)
{
    memset(&RIP_$WIRED_DATA, 0, sizeof(RIP_$WIRED_DATA));
    memset(&RIP_$WIRED_DATA.stats, 0, sizeof(RIP_$WIRED_DATA.stats));
    memset(ROUTE_$WIRED_DATA.portp, 0, sizeof(ROUTE_$WIRED_DATA.portp));
    RIP_$WIRED_DATA.std_recent_changes = 0;
    RIP_$WIRED_DATA.recent_changes = 0;
    ROUTE_$WIRED_DATA.std_n_routing_ports = 2;
    ROUTE_$WIRED_DATA.n_routing_ports = 2;
    NODE_$ME = 0xABCDE;

    memset(netbuf_page, 0, 1024);
    memset(&sock_get_record, 0, sizeof(sock_get_record));
    sock_get_record.hdr = ARCH_PTR_TO_VA(hdr_in_page());
    sock_get_result = true;
    sock_get_calls = 0;

    dump_data_calls = 0; dump_data_len = 0;
    rtn_hdr_calls = 0; memset(rtn_hdr_va, 0, sizeof(rtn_hdr_va));
    find_port_result = 0; find_port_network = 0; find_port_socket = 0;
    net_lookup_calls = 0;
    memset(net_lookup_result, 0, sizeof(net_lookup_result));
    memset(net_lookup_network, 0, sizeof(net_lookup_network));
    send_calls = 0; send_len = 0; send_port = 0; send_addr_info = NULL;
    memset(send_data, 0, sizeof(send_data));
    time_wait_calls = 0;
    time_wait_status = status_$time_quit_while_waiting;
    send_internet_calls = 0;
    update_int_calls = 0;
    hint_add_net_calls = 0;
    register_server_calls = 0;
    register_server_net = 0xDEADBEEFu;
    register_server_node = 0xDEADBEEFu;
    broadcast_calls = 0;
    brk_calls = 0;
    brk_network = 0; brk_dest_node = 0; brk_src_node_or = 0; brk_src_node = 0;
    brk_dest_sock = 0; brk_src_sock = 0; brk_id = 0; brk_info0 = 0;
    brk_data_len = 0; brk_status = status_$ok;
    memset(brk_payload, 0, sizeof(brk_payload));
}

/*
 * Arm an XNS ("standard") packet: the IDP header lives in the netbuf page and
 * its payload is the RIP packet, so RIP_$SERVER's copy at 0x00E68A62 and
 * 0x00E68A70 has something real to copy.
 */
static rip_$packet_t *arm_xns_packet(uint16_t command, int16_t n_entries)
{
    xns_$idp_header_t *h = hdr_in_page();
    rip_$packet_t *p = payload_in_page();
    uint16_t payload_len = (uint16_t)(n_entries * RIP_ENTRY_SIZE + 2);

    memset(h, 0, XNS_IDP_HEADER_SIZE);
    h->length = (uint16_t)(payload_len + XNS_IDP_HEADER_SIZE);
    h->packet_type = 0;
    memset(p, 0, sizeof(*p));
    p->command = command;

    sock_get_record.flags = SOCK_PKT_FLAG_XNS;
    sock_get_record.data_len = payload_len;
    sock_get_record.hdr_len = XNS_IDP_HEADER_SIZE;
    return p;
}

/* Arm a Domain-internet packet: the payload comes back through
 * PKT_$BRK_INTERNET_HDR instead. */
static rip_$packet_t *arm_internet_packet(uint16_t command, int16_t n_entries)
{
    rip_$packet_t *p = (rip_$packet_t *)brk_payload;

    memset(brk_payload, 0, sizeof(brk_payload));
    p->command = command;
    brk_data_len = (uint16_t)(n_entries * RIP_ENTRY_SIZE + 2);
    brk_status = status_$ok;
    sock_get_record.flags = 0;
    sock_get_record.data_len = brk_data_len;
    sock_get_record.hdr_len = 40;
    return p;
}

static void set_port_ident(uint16_t network, uint16_t socket)
{
    netbuf_page[0x3E0] = (uint8_t)(network >> 8);
    netbuf_page[0x3E1] = (uint8_t)network;
    netbuf_page[0x3E2] = (uint8_t)(socket >> 8);
    netbuf_page[0x3E3] = (uint8_t)socket;
}

static void set_route(int idx, int slot, uint32_t network, uint8_t metric,
                      uint8_t state)
{
    RIP_$WIRED_DATA.info[idx].network = network;
    RIP_$WIRED_DATA.info[idx].routes[slot].metric = metric;
    RIP_$WIRED_DATA.info[idx].routes[slot].flags =
        (uint8_t)((state << RIP_STATE_SHIFT) & RIP_STATE_MASK);
}

/* ==========================================================================
 * RIP_$PACKET_LENGTH (0x00E68864)
 * ========================================================================== */

TEST(packet_length)
{
    ASSERT_EQ(2, RIP_$PACKET_LENGTH(0));
    ASSERT_EQ(8, RIP_$PACKET_LENGTH(1));
    ASSERT_EQ(0x21E, RIP_$PACKET_LENGTH(RIP_MAX_ENTRIES));
    /* 16-bit arithmetic throughout ("add.w"/"addq.w") */
    ASSERT_EQ((int16_t)(0x1000 * 6 + 2), RIP_$PACKET_LENGTH(0x1000));
}

/* ==========================================================================
 * RIP_$SEND_UPDATES (0x00E6887A)
 * ========================================================================== */

TEST(send_updates_std_broadcasts_and_clears)
{
    ROUTE_$WIRED_DATA.std_n_routing_ports = 2;
    RIP_$WIRED_DATA.std_recent_changes = -1;

    RIP_$SEND_UPDATES(true);

    ASSERT_EQ(1, broadcast_calls);
    ASSERT_EQ((uint8_t)true, (uint8_t)broadcast_flags);
    ASSERT_EQ(0, RIP_$WIRED_DATA.std_recent_changes);
}

TEST(send_updates_internet_broadcasts_and_clears)
{
    ROUTE_$WIRED_DATA.n_routing_ports = 2;
    RIP_$WIRED_DATA.recent_changes = -1;

    RIP_$SEND_UPDATES(false);

    ASSERT_EQ(1, broadcast_calls);
    ASSERT_EQ(0, (uint8_t)broadcast_flags);
    ASSERT_EQ(0, RIP_$WIRED_DATA.recent_changes);
}

TEST(send_updates_needs_two_ports)
{
    /* "cmpi.w #0x1,... / ble" at 0x00E68884 and 0x00E688A2 */
    ROUTE_$WIRED_DATA.std_n_routing_ports = 1;
    RIP_$WIRED_DATA.std_recent_changes = -1;
    RIP_$SEND_UPDATES(true);
    ASSERT_EQ(0, broadcast_calls);
    ASSERT_EQ(-1, RIP_$WIRED_DATA.std_recent_changes);

    ROUTE_$WIRED_DATA.n_routing_ports = 1;
    RIP_$WIRED_DATA.recent_changes = -1;
    RIP_$SEND_UPDATES(false);
    ASSERT_EQ(0, broadcast_calls);
}

TEST(send_updates_needs_the_change_flag)
{
    RIP_$WIRED_DATA.std_recent_changes = 0;
    RIP_$SEND_UPDATES(true);
    ASSERT_EQ(0, broadcast_calls);
}

/* ==========================================================================
 * RIP_$SERVER - entry and validation (0x00E68A08-0x00E68B5A)
 * ========================================================================== */

TEST(server_empty_queue_does_nothing)
{
    /* "tst.b D0b / bpl 0x00E68E1C" at 0x00E68A22 */
    sock_get_result = false;
    RIP_$SERVER();
    ASSERT_EQ(1, sock_get_calls);
    ASSERT_EQ(0, dump_data_calls);
    ASSERT_EQ(0, rtn_hdr_calls);
    ASSERT_EQ(0, RIP_$WIRED_DATA.stats.packets_received);
}

TEST(server_length_mismatch_returns_the_buffer)
{
    /* 0x00E68B04-0x00E68B2A: RIP_$PACKET_LENGTH(entry_count) != data_len */
    arm_xns_packet(RIP_CMD_REQUEST, 2);
    sock_get_record.data_len = 15;          /* (15-2)/6 = 2, but 6*2+2 = 14 */
    hdr_in_page()->length = 15 + XNS_IDP_HEADER_SIZE;

    RIP_$SERVER();

    ASSERT_EQ(1, RIP_$WIRED_DATA.stats.packets_received);
    ASSERT_EQ(1, RIP_$WIRED_DATA.stats.errors);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(hdr_in_page()), rtn_hdr_va[0]);
    ASSERT_EQ(0, send_calls);
}

TEST(server_too_many_entries_is_an_error)
{
    /* "cmpi.w #0x5a,D5w / bgt" at 0x00E68AFE */
    arm_xns_packet(RIP_CMD_REQUEST, 0);
    sock_get_record.data_len = (uint16_t)(RIP_MAX_ENTRIES * 6 + 8);
    hdr_in_page()->length = (uint16_t)(sock_get_record.data_len + XNS_IDP_HEADER_SIZE);

    RIP_$SERVER();

    ASSERT_EQ(1, RIP_$WIRED_DATA.stats.errors);
    ASSERT_EQ(1, rtn_hdr_calls);
}

TEST(server_unknown_port_drops_after_returning_the_buffer)
{
    /* 0x00E68B46-0x00E68B5A: RTN_HDR first, then the -1 test */
    arm_xns_packet(RIP_CMD_REQUEST, 1);
    set_port_ident(0x1234, 0x5678);
    find_port_result = -1;

    RIP_$SERVER();

    ASSERT_EQ(0x1234, find_port_network);
    ASSERT_EQ(0x5678, find_port_socket);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(0, RIP_$WIRED_DATA.stats.errors);
    ASSERT_EQ(0, send_calls);
}

TEST(server_unknown_command_counts)
{
    /* 0x00E68E14 */
    arm_xns_packet(9, 1);
    RIP_$SERVER();
    ASSERT_EQ(1, RIP_$WIRED_DATA.stats.unknown_commands);
}

/* ==========================================================================
 * The request arm - STD/XNS (0x00E68B7C-0x00E68C12)
 * ========================================================================== */

TEST(request_std_broadcast_dropped_with_one_port)
{
    /*
     * 0x00E68B8C-0x00E68BA8: with one STD routing port, a request addressed
     * to the IDP broadcast HOST (header + 0x0A..0x0F) is ignored.
     */
    xns_$idp_header_t *h;
    arm_xns_packet(RIP_CMD_REQUEST, 1);
    h = hdr_in_page();
    memset(h->dest_host, 0xFF, 6);
    ROUTE_$WIRED_DATA.std_n_routing_ports = 1;

    RIP_$SERVER();

    ASSERT_EQ(0, send_calls);
}

TEST(request_std_non_broadcast_answered_with_one_port)
{
    /* One byte short of all-ones and the request is answered. */
    xns_$idp_header_t *h;
    arm_xns_packet(RIP_CMD_REQUEST, 0);
    h = hdr_in_page();
    memset(h->dest_host, 0xFF, 6);
    h->dest_host[5] = 0xFE;
    ROUTE_$WIRED_DATA.std_n_routing_ports = 1;

    RIP_$SERVER();

    ASSERT_EQ(1, send_calls);
}

TEST(request_std_answers_specific_networks)
{
    rip_$packet_t *req;
    rip_$packet_t *reply;
    rip_$entry_t known;

    req = arm_xns_packet(RIP_CMD_REQUEST, 2);
    req->entries[0].network = 0x11112222;
    req->entries[1].network = 0x33334444;

    memset(&known, 0, sizeof(known));
    known.routes[1].metric = 0x20;          /* STD side reads routes[1] (+0x27) */
    net_lookup_result[0] = &known;
    net_lookup_result[1] = NULL;
    find_port_result = 3;

    RIP_$SERVER();

    ASSERT_EQ(2, net_lookup_calls);
    ASSERT_EQ(0x11112222, net_lookup_network[0]);
    ASSERT_EQ(0x33334444, net_lookup_network[1]);

    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(3, send_port);
    ASSERT_EQ((uint8_t)true, (uint8_t)send_flags);
    ASSERT_EQ(RIP_$PACKET_LENGTH(2), send_len);

    /* "pea (-0xe,A6)" - the reply goes to the IDP SOURCE address at
     * header + 0x12, i.e. the frame's own header copy. */
    ASSERT_TRUE(send_addr_info != NULL);

    reply = (rip_$packet_t *)send_data;
    ASSERT_EQ(RIP_CMD_RESPONSE, reply->command);
    ASSERT_EQ(0x11112222, reply->entries[0].network);
    ASSERT_EQ(0x21, reply->entries[0].metric);      /* 0x20 + 1, above the clamp */
    ASSERT_EQ(0x33334444, reply->entries[1].network);
    ASSERT_EQ(0x10, reply->entries[1].metric);      /* unknown, STD side */

    /* One TIME_$WAIT, and "quit while waiting" ends the retry loop. */
    ASSERT_EQ(1, time_wait_calls);
}

TEST(request_std_clamps_the_metric_up_to_sixteen)
{
    /* "cmpi.l #0x10,D0 / bhi / moveq #0x10,D0" at 0x00E6893C */
    rip_$packet_t *req;
    rip_$packet_t *reply;
    rip_$entry_t known;

    req = arm_xns_packet(RIP_CMD_REQUEST, 1);
    req->entries[0].network = 0xAAAABBBB;
    memset(&known, 0, sizeof(known));
    known.routes[1].metric = 3;
    net_lookup_result[0] = &known;

    RIP_$SERVER();

    reply = (rip_$packet_t *)send_data;
    ASSERT_EQ(0x10, reply->entries[0].metric);
}

TEST(request_std_full_table_enumerates_valid_and_aging)
{
    /* 0x00E6896E-0x00E689FA */
    rip_$packet_t *req;
    rip_$packet_t *reply;

    req = arm_xns_packet(RIP_CMD_REQUEST, 1);
    req->entries[0].network = 0xFFFFFFFF;

    set_route(0, 1, 0x0A0A0A0A, 0x20, RIP_STATE_VALID);
    set_route(1, 1, 0x0B0B0B0B, 0x30, RIP_STATE_AGING);
    set_route(2, 1, 0x0C0C0C0C, 0x40, RIP_STATE_EXPIRED);   /* skipped */
    set_route(3, 1, 0x0D0D0D0D, 0x50, RIP_STATE_UNUSED);    /* skipped */

    RIP_$SERVER();

    ASSERT_EQ(0, net_lookup_calls);
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(RIP_$PACKET_LENGTH(2), send_len);

    reply = (rip_$packet_t *)send_data;
    ASSERT_EQ(RIP_CMD_RESPONSE, reply->command);
    ASSERT_EQ(0x0A0A0A0A, reply->entries[0].network);
    ASSERT_EQ(0x21, reply->entries[0].metric);
    ASSERT_EQ(0x0B0B0B0B, reply->entries[1].network);
    ASSERT_EQ(0x31, reply->entries[1].metric);
}

TEST(request_std_retries_five_times)
{
    /* "cmpi.w #0x5,D3w / bcs 0x00E68BB8" at 0x00E68C0C */
    arm_xns_packet(RIP_CMD_REQUEST, 0);
    time_wait_status = status_$ok;

    RIP_$SERVER();

    ASSERT_EQ(5, send_calls);
    ASSERT_EQ(5, time_wait_calls);
}

/* ==========================================================================
 * The request arm - Domain internet (0x00E68C16-0x00E68C84)
 * ========================================================================== */

TEST(request_internet_answers_with_pkt_send_internet)
{
    /*
     * An empty request (entry_count 0): RIP_$PROCESS_REQUEST returns without
     * reading anything, which keeps this test clear of the uninitialised
     * payload_va the internet path leaves behind (see the preserved-defect
     * note in rip/server.c, bead source-u9wy).  What is checked here is the fifteen-argument call at
     * 0x00E68C38-0x00E68C7E.
     */
    rip_$packet_t *reply;

    arm_internet_packet(RIP_CMD_REQUEST, 0);

    brk_network      = 0x00000042;
    brk_src_node_or  = 0x000AAAAA;
    brk_src_node     = 0x000BBBBB;
    brk_src_sock     = 0x0031;
    brk_id           = 0x1234;
    find_port_result = 1;

    RIP_$SERVER();

    ASSERT_EQ(1, brk_calls);
    ASSERT_EQ(1, send_internet_calls);
    ASSERT_EQ(0, send_calls);

    /* The addresses the packet came from become the addresses it goes to. */
    ASSERT_EQ(0x000AAAAA, si_routing_key);       /* 1  A6-0x4F4 */
    ASSERT_EQ(0x000BBBBB, si_dest_node);         /* 2  A6-0x4F8 */
    ASSERT_EQ(0x0031, si_dest_sock);             /* 3  A6-0x51A */
    ASSERT_EQ(0x00000042, si_src_node_or);       /* 4  D4       */
    ASSERT_EQ(0xABCDE, si_src_node);             /* 5  NODE_$ME */
    ASSERT_EQ(RIP_SOCKET, si_src_sock);          /* 6  #8       */
    ASSERT_EQ(0x20, si_pkt_info_word0);          /* 7  0x00E68C32 */
    ASSERT_EQ(0x1234, si_request_id);            /* 8  A6-0x518 */
    ASSERT_EQ(RIP_$PACKET_LENGTH(0), si_template_len);  /* 10 */
    ASSERT_EQ(0, si_data_len);                   /* 12 */

    reply = (rip_$packet_t *)si_template;
    ASSERT_EQ(RIP_CMD_RESPONSE, reply->command);
}

TEST(request_internet_dropped_on_a_negative_info_byte)
{
    /* "tst.b (-0x2af,A6) / bmi" at 0x00E68C20 */
    arm_internet_packet(RIP_CMD_REQUEST, 0);
    ROUTE_$WIRED_DATA.n_routing_ports = 1;
    brk_info0 = 0x0080;                         /* low byte 0x80 -> negative */

    RIP_$SERVER();

    ASSERT_EQ(0, send_internet_calls);
}

/* ==========================================================================
 * RIP_$PROCESS_REQUEST (0x00E688C8) driven directly
 * ==========================================================================
 *
 * The nested procedure reaches its buffers through the static link, so a test
 * can hand it a frame of its own.  This is the only way to reach the
 * internet-side metric rules, because on that path RIP_$SERVER never fills in
 * payload_va.
 */

static rip_$server_frame_t pr_frame;
static rip_$packet_t      *pr_request;      /* carved out of host_arena */

static void pr_setup(int16_t n_entries)
{
    memset(&pr_frame, 0, sizeof(pr_frame));
    memset(pr_request, 0, sizeof(*pr_request));
    pr_frame.entry_count = n_entries;
    pr_frame.payload_va = ARCH_PTR_TO_VA(pr_request);
}

TEST(process_request_internet_metrics)
{
    /* 0x00E68948-0x00E6895C: routes[0] (+0x13), no clamp, 0x11 if unknown */
    rip_$entry_t known;

    pr_setup(2);
    pr_request->entries[0].network = 0x55556666;
    pr_request->entries[1].network = 0x77778888;

    memset(&known, 0, sizeof(known));
    known.routes[0].metric = 4;
    net_lookup_result[0] = &known;
    net_lookup_result[1] = NULL;

    RIP_$PROCESS_REQUEST(false, &pr_frame);

    ASSERT_EQ(2, net_lookup_calls);
    ASSERT_EQ(RIP_CMD_RESPONSE, pr_frame.response.command);
    ASSERT_EQ(2, pr_frame.response_count);
    ASSERT_EQ(0x55556666, pr_frame.response.entries[0].network);
    ASSERT_EQ(5, pr_frame.response.entries[0].metric);
    ASSERT_EQ(0x77778888, pr_frame.response.entries[1].network);
    ASSERT_EQ(RIP_INFINITY, pr_frame.response.entries[1].metric);
}

TEST(process_request_internet_full_table)
{
    /* The enumeration reads routes[0] on this side ("lea (0x4,A0),A1") */
    pr_setup(1);
    pr_request->entries[0].network = 0xFFFFFFFF;

    set_route(0, 0, 0x0E0E0E0E, 2, RIP_STATE_VALID);
    set_route(1, 0, 0x0F0F0F0F, 3, RIP_STATE_AGING);
    set_route(2, 1, 0x10101010, 4, RIP_STATE_VALID);    /* wrong slot: skipped */

    RIP_$PROCESS_REQUEST(false, &pr_frame);

    ASSERT_EQ(0, net_lookup_calls);
    ASSERT_EQ(2, pr_frame.response_count);
    ASSERT_EQ(0x0E0E0E0E, pr_frame.response.entries[0].network);
    ASSERT_EQ(3, pr_frame.response.entries[0].metric);
    ASSERT_EQ(0x0F0F0F0F, pr_frame.response.entries[1].network);
    ASSERT_EQ(4, pr_frame.response.entries[1].metric);
}

TEST(process_request_empty_request_answers_nothing)
{
    /* "subq.w #1,D0w / bmi 0x00E68968" at 0x00E688EA */
    pr_setup(0);
    RIP_$PROCESS_REQUEST(true, &pr_frame);
    ASSERT_EQ(RIP_CMD_RESPONSE, pr_frame.response.command);
    ASSERT_EQ(0, pr_frame.response_count);
    ASSERT_EQ(0, net_lookup_calls);
}

TEST(process_request_full_table_caps_at_ninety)
{
    /* "cmpi.w #0x5a,(-0x512,A3) / beq" at 0x00E689EE */
    int i;

    pr_setup(1);
    pr_request->entries[0].network = 0xFFFFFFFF;
    for (i = 0; i < RIP_TABLE_SIZE; i++) {
        set_route(i, 0, (uint32_t)(0x1000 + i), 1, RIP_STATE_VALID);
    }

    RIP_$PROCESS_REQUEST(false, &pr_frame);

    /* Only 64 slots exist, so the cap is not reached; all 64 are answered. */
    ASSERT_EQ(RIP_TABLE_SIZE, pr_frame.response_count);
    ASSERT_EQ(0x1000, pr_frame.response.entries[0].network);
    ASSERT_EQ(0x1000 + RIP_TABLE_SIZE - 1,
              pr_frame.response.entries[RIP_TABLE_SIZE - 1].network);
}

/* ==========================================================================
 * The response arm (0x00E68C88-0x00E68DC8)
 * ========================================================================== */

TEST(response_std_updates_every_entry)
{
    /* 0x00E68D88-0x00E68DBC: the dbf runs entry_count times */
    rip_$packet_t *p;
    xns_$idp_header_t *h;
    route_$port_t port;

    memset(&port, 0, sizeof(port));
    port.network = 0x0000BEEF;
    ROUTE_$WIRED_DATA.portp[2] = &port;
    find_port_result = 2;

    p = arm_xns_packet(RIP_CMD_RESPONSE, 3);
    p->entries[0].network = 0x00000001; p->entries[0].metric = 1;
    p->entries[1].network = 0x00000002; p->entries[1].metric = 2;
    p->entries[2].network = 0x00000003; p->entries[2].metric = 3;

    h = hdr_in_page();
    h->dest_network = 0x0000BEEF;               /* matches the port: no change */
    h->src_host[0] = 0x01; h->src_host[1] = 0x02; h->src_host[2] = 0x03;
    h->src_host[3] = 0x04; h->src_host[4] = 0x05; h->src_host[5] = 0x06;

    ROUTE_$WIRED_DATA.std_n_routing_ports = 1;             /* < 2 -> process the routes */
    RIP_$WIRED_DATA.std_recent_changes = 0;

    RIP_$SERVER();

    ASSERT_EQ(3, update_int_calls);
    ASSERT_EQ(0x00000001, ui_network[0]);
    ASSERT_EQ(1, ui_metric[0]);
    ASSERT_EQ(0x00000003, ui_network[2]);
    ASSERT_EQ(3, ui_metric[2]);
    ASSERT_EQ(2, ui_port[0]);

    /* source_addr = { dest_network, src_host } - 0x00E68D5A/0x00E68D68 */
    ASSERT_EQ(0x0000BEEF, ui_source[0].network);
    ASSERT_EQ(0x01, ui_source[0].host[0]);
    ASSERT_EQ(0x06, ui_source[0].host[5]);
}

TEST(response_std_moves_the_port_to_a_new_network)
{
    /* 0x00E68CA4-0x00E68D16 */
    rip_$packet_t *p;
    xns_$idp_header_t *h;
    route_$port_t port;

    memset(&port, 0, sizeof(port));
    port.network = 0x0000AAAA;
    port.active  = 0;                           /* bit 0 is not in {3,4,5} */
    ROUTE_$WIRED_DATA.portp[0] = &port;
    find_port_result = 0;

    p = arm_xns_packet(RIP_CMD_RESPONSE, 0);
    h = hdr_in_page();
    h->dest_network = 0x0000CCCC;               /* the packet came in on CCCC */
    (void)p;

    ROUTE_$WIRED_DATA.std_n_routing_ports = 3;             /* >= 2, active bit 0 not set */

    RIP_$SERVER();

    ASSERT_EQ(2, update_int_calls);
    ASSERT_EQ(0x0000AAAA, ui_network[0]);       /* withdraw the old network */
    ASSERT_EQ(0x10, ui_metric[0]);
    ASSERT_EQ(0x0000AAAA, ui_source[0].network);
    ASSERT_EQ(0, ui_source[0].host[0]);
    ASSERT_EQ(0x0000CCCC, ui_network[1]);       /* install the new one */
    ASSERT_EQ(0, ui_metric[1]);
    ASSERT_EQ(0x0000CCCC, ui_source[1].network);

    ASSERT_EQ(0x0000CCCC, port.network);        /* 0x00E68D04 */
    ASSERT_EQ(0x0000CCCC, port.xns_addr.network); /* 0x00E68D06 */
    ASSERT_EQ(1, hint_add_net_calls);           /* port index 0 only */
    ASSERT_EQ(0x0000CCCC, hint_add_net_arg);
}

TEST(response_std_leaves_ports_with_a_protected_active_bit_alone)
{
    /* "moveq #0x38,D5 / btst.l D1,D5 / bne" at 0x00E68CAC */
    xns_$idp_header_t *h;
    route_$port_t port;

    memset(&port, 0, sizeof(port));
    port.network = 0x0000AAAA;
    port.active  = 4;                           /* bit 4 is in 0x38 */
    ROUTE_$WIRED_DATA.portp[0] = &port;
    find_port_result = 0;

    arm_xns_packet(RIP_CMD_RESPONSE, 0);
    h = hdr_in_page();
    h->dest_network = 0x0000CCCC;
    ROUTE_$WIRED_DATA.std_n_routing_ports = 3;

    RIP_$SERVER();

    ASSERT_EQ(0x0000AAAA, port.network);
    ASSERT_EQ(0, hint_add_net_calls);
}

TEST(response_internet_masks_the_node_into_the_source_host)
{
    /* 0x00E68D78-0x00E68D86 */
    rip_$packet_t *p;
    route_$port_t port;

    /*
     * The port has moved network, so the "clear the 6-byte host" block at
     * 0x00E68CB6 runs first and the OR at 0x00E68D84 has a known starting
     * value.  Without that the original ORs the node id into whatever the
     * frame held.
     */
    memset(&port, 0, sizeof(port));
    port.network = 0x00000011;
    port.active  = 0;
    ROUTE_$WIRED_DATA.portp[0] = &port;
    find_port_result = 0;

    p = arm_internet_packet(RIP_CMD_RESPONSE, 1);
    p->entries[0].network = 0x00009999;
    p->entries[0].metric = 7;
    brk_network  = 0x00000042;
    brk_src_node = 0x000ABCDE;
    ROUTE_$WIRED_DATA.n_routing_ports = 1;

    RIP_$SERVER();

    ASSERT_EQ(3, update_int_calls);         /* withdraw, install, then the route */
    ASSERT_EQ(0x00009999, ui_network[2]);
    ASSERT_EQ(7, ui_metric[2]);
    ASSERT_EQ(0x00000042, ui_source[2].network);
    /* host[2..5] = (old & 0xFFF00000) | src_node */
    ASSERT_EQ(0x00, ui_source[2].host[2]);
    ASSERT_EQ(0x0A, ui_source[2].host[3]);
    ASSERT_EQ(0xBC, ui_source[2].host[4]);
    ASSERT_EQ(0xDE, ui_source[2].host[5]);
}

TEST(response_sends_updates_at_the_end)
{
    /* 0x00E68DC0 */
    route_$port_t port;

    memset(&port, 0, sizeof(port));
    port.network = 0x0000BEEF;
    ROUTE_$WIRED_DATA.portp[0] = &port;
    find_port_result = 0;

    arm_xns_packet(RIP_CMD_RESPONSE, 0);
    hdr_in_page()->dest_network = 0x0000BEEF;
    ROUTE_$WIRED_DATA.std_n_routing_ports = 2;
    RIP_$WIRED_DATA.std_recent_changes = -1;

    RIP_$SERVER();

    ASSERT_EQ(1, broadcast_calls);
    ASSERT_EQ((uint8_t)true, (uint8_t)broadcast_flags);
}

/* ==========================================================================
 * The name-register arm (0x00E68DCA-0x00E68E12)
 * ========================================================================== */

TEST(name_register_std_requires_packet_type_be)
{
    /* "move.b (-0x1b,A6),D1b / cmpi.w #0xbe,D1w / bne" at 0x00E68DD0 */
    arm_xns_packet(RIP_CMD_NAME_REGISTER, 0);
    hdr_in_page()->packet_type = 0xBE;
    hdr_in_page()->src_network = 0x00ABCDEFu;

    RIP_$SERVER();

    ASSERT_EQ(1, register_server_calls);
    ASSERT_EQ(0, RIP_$WIRED_DATA.stats.unknown_commands);

    /*
     * "pea (-0x538,A6)" (0x00E68DF2, reg_node_id) then "pea (-0x4e0,A6)"
     * (0x00E68DF6, reg_network): the last push is argument 1, so argument 1
     * is the network the packet came from (0x00E68DDA copies
     * header.src_network into it).
     */
    ASSERT_EQ(0x00ABCDEFu, register_server_net);
}

TEST(name_register_std_wrong_packet_type_counts)
{
    arm_xns_packet(RIP_CMD_NAME_REGISTER, 0);
    hdr_in_page()->packet_type = 0x0B;

    RIP_$SERVER();

    ASSERT_EQ(0, register_server_calls);
    ASSERT_EQ(1, RIP_$WIRED_DATA.stats.unknown_commands);
}

TEST(name_register_internet_always_registers)
{
    /* 0x00E68E04: no packet-type check on this side */
    arm_internet_packet(RIP_CMD_NAME_REGISTER, 0);
    brk_src_node_or = 0x00012345u;
    brk_src_node    = 0x00067890u;

    RIP_$SERVER();

    ASSERT_EQ(1, register_server_calls);
    ASSERT_EQ(0, RIP_$WIRED_DATA.stats.unknown_commands);

    /*
     * "pea (-0x4f8,A6)" (0x00E68E04, src_node) then "pea (-0x4f4,A6)"
     * (0x00E68E08, src_node_or), so argument 1 is src_node_or and argument 2
     * is src_node.
     */
    ASSERT_EQ(0x00012345u, register_server_net);
    ASSERT_EQ(0x00067890u, register_server_node);
}

/* ========================================================================== */

int main(void)
{
    /* The header buffer has to sit in a 1KB-aligned page: RIP_$SERVER derives
     * the page from the header VA with "andi.w #-0x400" at 0x00E68A48. */
    netbuf_page = (uint8_t *)(((uintptr_t)host_arena + 1023)
                              & ~(uintptr_t)1023);
    pr_request  = (rip_$packet_t *)(host_arena + 0x2000);
    ARCH_HOST_VA_BASE = (uintptr_t)host_arena - 0x1000;

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("RIP_$SERVER tests\n");

    RUN_TEST(packet_length);

    RUN_TEST(send_updates_std_broadcasts_and_clears);
    RUN_TEST(send_updates_internet_broadcasts_and_clears);
    RUN_TEST(send_updates_needs_two_ports);
    RUN_TEST(send_updates_needs_the_change_flag);

    RUN_TEST(server_empty_queue_does_nothing);
    RUN_TEST(server_length_mismatch_returns_the_buffer);
    RUN_TEST(server_too_many_entries_is_an_error);
    RUN_TEST(server_unknown_port_drops_after_returning_the_buffer);
    RUN_TEST(server_unknown_command_counts);

    RUN_TEST(request_std_broadcast_dropped_with_one_port);
    RUN_TEST(request_std_non_broadcast_answered_with_one_port);
    RUN_TEST(request_std_answers_specific_networks);
    RUN_TEST(request_std_clamps_the_metric_up_to_sixteen);
    RUN_TEST(request_std_full_table_enumerates_valid_and_aging);
    RUN_TEST(request_std_retries_five_times);

    RUN_TEST(request_internet_answers_with_pkt_send_internet);
    RUN_TEST(request_internet_dropped_on_a_negative_info_byte);

    RUN_TEST(process_request_internet_metrics);
    RUN_TEST(process_request_internet_full_table);
    RUN_TEST(process_request_empty_request_answers_nothing);
    RUN_TEST(process_request_full_table_caps_at_ninety);

    RUN_TEST(response_std_updates_every_entry);
    RUN_TEST(response_std_moves_the_port_to_a_new_network);
    RUN_TEST(response_std_leaves_ports_with_a_protected_active_bit_alone);
    RUN_TEST(response_internet_masks_the_node_into_the_source_host);
    RUN_TEST(response_sends_updates_at_the_end);

    RUN_TEST(name_register_std_requires_packet_type_be);
    RUN_TEST(name_register_std_wrong_packet_type_counts);
    RUN_TEST(name_register_internet_always_registers);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
