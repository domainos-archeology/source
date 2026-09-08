/*
 * route/test/test_service.c - Unit tests for ROUTE_$SERVICE (0x00E6A030).
 *
 * The test compiles the real route/service.c with every routine it calls
 * scripted, so the arms the disassembly review flagged can be driven from C:
 *
 *   - the port-0 re-announce and its store into port0+0x20 (0x00E6A0D4)
 *   - which hop-count cell each of the three RIP_$UPDATE_D pairs passes
 *     (0x00E69FB0 = 0x0010 for the removal pair, 0x00E6A5D8 = 0 for the other
 *     two)
 *   - the attach callback's five arguments and word result slot (0x00E6A442)
 *   - the status-restore rule at 0x00E6A518: only a non-zero status restores
 *     the old status; a zero status with a new status other than 1 falls
 *     through untouched.
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

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAILED\n    %s at line %d\n", #cond, __LINE__);           \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals and mocks the code under test links against
 * ========================================================================== */

#include "route/route_internal.h"
#include "net_io/net_io.h"

route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
route_$port_t *ROUTE_$PORTP[ROUTE_$MAX_PORTS];
uint32_t ROUTE_$PORT;
ml_$exclusion_t ROUTE_$SERVICE_MUTEX;
int16_t ROUTE_$N_USER_PORTS;
int16_t RIP_$STD_IDP_CHANNEL;
uint16_t APP_$STD_IDP_CHANNEL;

net_io_$driver_t NET_IO_$NIL_DRIVER[1];
net_io_$driver_t NET_IO_$USER_DRIVER[1];

/* --- exclusion ---------------------------------------------------------- */

static int excl_start_calls;
static int excl_stop_calls;

void ML_$EXCLUSION_START(ml_$exclusion_t *excl) { (void)excl; excl_start_calls++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)  { (void)excl; excl_stop_calls++; }

/* --- route helpers ------------------------------------------------------ */

/*
 * route_$close_port is a file static inside route/service.c now (bead
 * source-kc3d), so it is exercised for real rather than mocked; the
 * helpers it calls (ROUTE_$FIND_PORT, ROUTE_$SHORT_PORT, RIP_$UPDATE_D,
 * ROUTE_$DECREMENT_PORT, SOCK_$CLOSE, ROUTE_$CLEANUP_WIRED) are mocked
 * below like every other callee.
 */
static int sock_close_calls;
static uint16_t sock_close_socket;
static int cleanup_wired_calls;

void ROUTE_$CLEANUP_WIRED(void) { cleanup_wired_calls++; }

void SOCK_$CLOSE(uint16_t sock)
{
    sock_close_calls++;
    sock_close_socket = sock;
}

static int short_port_calls;
static route_$port_t *short_port_src_seen[8];
static route_$short_port_t *short_port_dst_seen[8];

void ROUTE_$SHORT_PORT(route_$port_t *port_struct,
                       route_$short_port_t *short_info)
{
    if (short_port_calls < 8) {
        short_port_src_seen[short_port_calls] = port_struct;
        short_port_dst_seen[short_port_calls] = short_info;
    }
    short_port_calls++;
    short_info->network = port_struct->network;
    short_info->status = port_struct->active;
    short_info->port_type = port_struct->port_type;
    short_info->socket = port_struct->socket;
    short_info->queue_length = port_struct->socket2;
}

static int find_port_calls;
static int16_t find_port_result;
static uint16_t find_port_net_seen;
static int32_t find_port_sock_seen;

int16_t ROUTE_$FIND_PORT(uint16_t network, int32_t socket)
{
    find_port_calls++;
    find_port_net_seen = network;
    find_port_sock_seen = socket;
    return find_port_result;
}

static int wire_calls;
void route_$wire_routing_area(void) { wire_calls++; }

static int announce_calls;
static uint32_t announce_net_seen;
void ROUTE_$ANNOUNCE_NET(uint32_t network)
{
    announce_calls++;
    announce_net_seen = network;
}

static int decrement_calls;
static int8_t decrement_a1[8];
static int16_t decrement_a2[8];
static int8_t decrement_a3[8];

void ROUTE_$DECREMENT_PORT(int8_t delete_flag, int16_t port_index,
                           int8_t port_type_flag)
{
    if (decrement_calls < 8) {
        decrement_a1[decrement_calls] = delete_flag;
        decrement_a2[decrement_calls] = port_index;
        decrement_a3[decrement_calls] = port_type_flag;
    }
    decrement_calls++;
}

static int init_routing_calls;
static int16_t init_routing_port[8];
static int8_t init_routing_type[8];

void ROUTE_$INIT_ROUTING(int16_t port_index, int8_t port_type)
{
    if (init_routing_calls < 8) {
        init_routing_port[init_routing_calls] = port_index;
        init_routing_type[init_routing_calls] = port_type;
    }
    init_routing_calls++;
}

/* --- RIP ---------------------------------------------------------------- */

static int rip_update_calls;
static const uint32_t *rip_net_seen[8];
static const uint16_t *rip_hop_seen[8];
static const boolean *rip_op_seen[8];
static status_$t *rip_status_seen[8];
static uint16_t rip_hop_value[8];

void RIP_$UPDATE_D(const uint32_t *network_ptr, void *source,
                   const uint16_t *hop_count_ptr, const uint8_t *port_info,
                   const boolean *flags_ptr, status_$t *status_ret)
{
    (void)source;
    (void)port_info;
    if (rip_update_calls < 8) {
        rip_net_seen[rip_update_calls] = network_ptr;
        rip_hop_seen[rip_update_calls] = hop_count_ptr;
        rip_hop_value[rip_update_calls] = *hop_count_ptr;
        rip_op_seen[rip_update_calls] = flags_ptr;
        rip_status_seen[rip_update_calls] = status_ret;
    }
    rip_update_calls++;
}

static int rip_send_calls;
static boolean rip_send_arg[4];

void RIP_$SEND_UPDATES(boolean is_std)
{
    if (rip_send_calls < 4) {
        rip_send_arg[rip_send_calls] = is_std;
    }
    rip_send_calls++;
}

/* --- other subsystems --------------------------------------------------- */

static int create_port_calls;
static int16_t create_port_result;
static status_$t create_port_status;
static int16_t create_port_type_seen;
static uint16_t create_port_unit_seen;
static void *create_port_driver_seen;
static uint16_t create_port_qlen_seen;

int16_t NET_IO_$CREATE_PORT(int16_t port_type, uint16_t unit, void *driver,
                            uint16_t queue_length, status_$t *status_ret)
{
    create_port_calls++;
    create_port_type_seen = port_type;
    create_port_unit_seen = unit;
    create_port_driver_seen = driver;
    create_port_qlen_seen = queue_length;
    *status_ret = create_port_status;
    return create_port_result;
}

static int hint_add_net_calls;
static uint32_t hint_add_net_seen;
void HINT_$ADD_NET(uint32_t net_port)
{
    hint_add_net_calls++;
    hint_add_net_seen = net_port;
}

static int idp_add_calls;
static uint16_t *idp_add_chan[4];
static uint16_t idp_add_port[4];
static int idp_del_calls;
static uint16_t *idp_del_chan[4];
static uint16_t idp_del_port[4];

void XNS_IDP_$OS_ADD_PORT(uint16_t *channel, uint16_t *port,
                          status_$t *status_ret)
{
    if (idp_add_calls < 4) {
        idp_add_chan[idp_add_calls] = channel;
        idp_add_port[idp_add_calls] = *port;
    }
    idp_add_calls++;
    *status_ret = status_$ok;
}

void XNS_IDP_$OS_DELETE_PORT(uint16_t *channel, uint16_t *port,
                             status_$t *status_ret)
{
    if (idp_del_calls < 4) {
        idp_del_chan[idp_del_calls] = channel;
        idp_del_port[idp_del_calls] = *port;
    }
    idp_del_calls++;
    *status_ret = status_$ok;
}

/* --- the driver record and its three entries ---------------------------- */

static route_$driver_info_t test_driver;
/* route_$port_t.driver_stats points at one of these; route_$close_port
 * clears the high byte of its flags word (0x00E69F9A). */
static route_$port_stats_t test_stats;

static int leave_calls;
static uint16_t *leave_socket_seen;
static status_$t leave_status;          /* stored through status_ret */

static void test_leave_fn(uint16_t *socket_ptr, status_$t *status_ret)
{
    leave_calls++;
    leave_socket_seen = socket_ptr;
    *status_ret = leave_status;
}

static int enter_calls;
static uint16_t *enter_socket_seen;
static status_$t enter_status;

static void test_enter_fn(uint16_t *socket_ptr, status_$t *status_ret)
{
    enter_calls++;
    enter_socket_seen = socket_ptr;
    *status_ret = enter_status;
}

static int attach_calls;
static uint16_t *attach_socket_seen;
static const uint16_t *attach_rec_seen;
static uint16_t attach_request_seen;
static void *attach_out4_seen;
static status_$t *attach_out5_seen;

static int16_t test_attach_fn(uint16_t *socket_ptr, const uint16_t *service_rec,
                              uint16_t request, void *out4, void *out5)
{
    attach_calls++;
    attach_socket_seen = socket_ptr;
    attach_rec_seen = service_rec;
    attach_request_seen = request;
    attach_out4_seen = out4;
    attach_out5_seen = (status_$t *)out5;
    return 0;
}

#include "../service.c"

/* ==========================================================================
 * Harness
 * ========================================================================== */

static route_$short_port_t request;
static status_$t call_status;
static uint16_t op_word;

static void reset_state(void)
{
    int i;

    /*
     * route_$port_t.driver_info and the three driver entries inside
     * route_$driver_info_t are 32-bit target VAs, so anchor the host arena
     * below every address the test converts.  ARCH_PTR_TO_VA only round-trips
     * for addresses at or above the base, so take the lowest of them.
     */
    {
        uintptr_t addrs[5];
        uintptr_t lowest;
        size_t n;

        addrs[0] = (uintptr_t)&test_driver;
        addrs[1] = (uintptr_t)test_leave_fn;
        addrs[2] = (uintptr_t)test_enter_fn;
        addrs[3] = (uintptr_t)test_attach_fn;
        addrs[4] = (uintptr_t)&test_stats;
        lowest = addrs[0];
        for (n = 1; n < 5; n++) {
            if (addrs[n] < lowest) {
                lowest = addrs[n];
            }
        }
        ARCH_HOST_VA_BASE = lowest - 0x1000;
    }

    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    for (i = 0; i < ROUTE_$MAX_PORTS; i++) {
        ROUTE_$PORTP[i] = &ROUTE_$PORT_ARRAY[i];
    }
    memset(&test_driver, 0, sizeof(test_driver));
    memset(&test_stats, 0, sizeof(test_stats));
    memset(&request, 0, sizeof(request));

    ROUTE_$PORT = 0;
    ROUTE_$N_USER_PORTS = 0;
    RIP_$STD_IDP_CHANNEL = -1;
    APP_$STD_IDP_CHANNEL = 0xFFFF;

    excl_start_calls = 0;
    excl_stop_calls = 0;
    sock_close_calls = 0;
    sock_close_socket = 0;
    cleanup_wired_calls = 0;
    short_port_calls = 0;
    memset(short_port_src_seen, 0, sizeof(short_port_src_seen));
    memset(short_port_dst_seen, 0, sizeof(short_port_dst_seen));
    find_port_calls = 0;
    find_port_result = 1;
    wire_calls = 0;
    announce_calls = 0;
    decrement_calls = 0;
    init_routing_calls = 0;
    rip_update_calls = 0;
    memset(rip_hop_value, 0xEE, sizeof(rip_hop_value));
    rip_send_calls = 0;
    create_port_calls = 0;
    create_port_result = 1;
    create_port_status = status_$ok;
    hint_add_net_calls = 0;
    idp_add_calls = 0;
    idp_del_calls = 0;
    leave_calls = 0;
    leave_status = status_$ok;
    enter_calls = 0;
    enter_status = status_$ok;
    attach_calls = 0;

    call_status = 0x5A5A5A5A;
    op_word = 0;
}

static void call_service(void)
{
    ROUTE_$SERVICE(&op_word, &request, &call_status);
}

/* Give port `idx` the scripted driver record. */
static void attach_driver(int idx)
{
    ROUTE_$PORT_ARRAY[idx].driver_info = ARCH_PTR_TO_VA(&test_driver);
}

/* ==========================================================================
 * Entry arms
 * ========================================================================== */

/*
 * 0x00E6A056-0x00E6A064: bit 3 runs the nested close-port procedure and
 * leaves through the unlock at 0x00E6A274 without the RIP/reply tail.
 * ROUTE_$FIND_PORT is made to fail so the arm stops at 0x00E69EF2 and the
 * test observes only the entry/exit shape.
 */
TEST(close_port_bit_returns_early)
{
    reset_state();
    op_word = SERVICE_OP_CLOSE_PORT;
    find_port_result = -1;
    call_service();

    ASSERT_EQ(1, find_port_calls);
    ASSERT_EQ(1, excl_start_calls);
    ASSERT_EQ(1, excl_stop_calls);
    ASSERT_EQ(0, rip_send_calls);
    ASSERT_EQ(0, short_port_calls);
    ASSERT_EQ(status_$internet_unknown_network_port, call_status);
}

/*
 * source-kc3d.  The nested procedure reads the caller's frame, not its own
 * arguments:
 *
 *   0x00E69ECE / 0x00E69EDA  ROUTE_$FIND_PORT's arguments are port_info's
 *                            PORT TYPE word (+0x06) and its sign-extended
 *                            socket word (+0x08), NOT a network
 *   0x00E69F42 / 0x00E69F62  the short_port it fills and hands to
 *                            RIP_$UPDATE_D is ROUTE_$SERVICE's own local
 *   0x00E69F66               the hop count is the 0x0010 cell at
 *                            0x00E69FB0, not zero
 */
TEST(close_port_uses_the_parents_frame)
{
    route_$port_t *port;

    reset_state();
    port = &ROUTE_$PORT_ARRAY[1];
    port->network   = 0x00ABCDEF;
    port->active    = 4;
    port->port_type = ROUTE_PORT_TYPE_ROUTING;
    port->socket    = 0x0031;
    port->driver_stats = ARCH_PTR_TO_VA(&test_stats);
    test_stats.flags = 0xFF00;

    request.port_type = 2;                  /* port_info+0x06 */
    request.socket    = 0x1234;             /* port_info+0x08 */

    op_word = SERVICE_OP_CLOSE_PORT;
    find_port_result = 1;
    ROUTE_$N_USER_PORTS = 3;
    call_service();

    /* 0x00E69ED2/0x00E69EDE: the type word and the sign-extended socket. */
    ASSERT_EQ(2, find_port_net_seen);
    ASSERT_EQ(0x1234, find_port_sock_seen);

    /* 0x00E69F42: the parent's short_port local, not one of its own. */
    ASSERT_EQ(1, short_port_calls);
    ASSERT_TRUE(short_port_src_seen[0] == port);

    /* 0x00E69F66 -> 0x00E69FB0, the hop-count 0x0010 cell. */
    ASSERT_EQ(1, rip_update_calls);
    ASSERT_EQ(0x0010, rip_hop_value[0]);

    /* 0x00E69F7A-0x00E69F9E: the type-2 tail. */
    ASSERT_EQ(1, sock_close_calls);
    ASSERT_EQ(0x0031, sock_close_socket);
    ASSERT_EQ(2, ROUTE_$N_USER_PORTS);
    ASSERT_EQ(1, cleanup_wired_calls);
    ASSERT_EQ(0x0000, test_stats.flags);    /* high byte cleared */

    /* 0x00E69FA0 */
    ASSERT_EQ(0, port->active);
}

/* 0x00E6A066-0x00E6A0B4: the three user-port rejections, in order. */
TEST(user_port_validation)
{
    reset_state();
    op_word = SERVICE_OP_USER_PORT;
    request.port_type = 1;
    call_service();
    ASSERT_EQ(status_$route_illegal_op_for_port_type, call_status);
    ASSERT_EQ(0, rip_send_calls);

    reset_state();
    op_word = SERVICE_OP_USER_PORT;
    request.port_type = 2;
    call_service();
    ASSERT_EQ(status_$route_create_flag_required, call_status);

    reset_state();
    op_word = SERVICE_OP_USER_PORT | SERVICE_OP_CREATE_PORT;
    request.port_type = 2;
    request.queue_length = 0x21;
    call_service();
    ASSERT_EQ(status_$route_queue_length_too_large, call_status);

    /* Exactly 0x20 is accepted ("bls" is an unsigned <=). */
    reset_state();
    op_word = SERVICE_OP_USER_PORT | SERVICE_OP_CREATE_PORT;
    request.port_type = 2;
    request.queue_length = 0x20;
    call_service();
    ASSERT_EQ(status_$ok, call_status);
    ASSERT_EQ(1, create_port_calls);
    ASSERT_EQ(0x20, create_port_qlen_seen);
}

/* ==========================================================================
 * Port 0 re-announce
 * ========================================================================== */

/*
 * 0x00E6A0B6-0x00E6A128.  The mask 0x3C selects statuses 2..5.  The pair uses
 * the zero hop-count cell, and the store at 0x00E6A0D4 refreshes port 0's own
 * XNS network word from port 0's network.
 */
TEST(port0_reannounce_stores_xns_network)
{
    reset_state();
    ROUTE_$PORT_ARRAY[0].active = 4;              /* in 0x3C */
    ROUTE_$PORT_ARRAY[0].network = 0x11223344;
    ROUTE_$PORT_ARRAY[0].xns_addr.network = 0;
    find_port_result = 1;

    call_service();

    ASSERT_EQ(0x11223344, ROUTE_$PORT_ARRAY[0].xns_addr.network);
    ASSERT_EQ(2, rip_update_calls);
    /* Both calls take the zero hop-count cell at 0x00E6A5D8. */
    ASSERT_EQ(0x0000, rip_hop_value[0]);
    ASSERT_EQ(0x0000, rip_hop_value[1]);
    /* ... and the caller's own status pointer, not a local. */
    ASSERT_EQ((uintptr_t)&call_status, (uintptr_t)rip_status_seen[0]);
    ASSERT_EQ((uintptr_t)&call_status, (uintptr_t)rip_status_seen[1]);
    /* The op cells are the add byte then the delete byte. */
    ASSERT_EQ(0x00, *rip_op_seen[0]);
    ASSERT_EQ((int8_t)0xFF, *rip_op_seen[1]);
}

/* A status outside 0x3C (1, or 0) skips the announcement entirely. */
TEST(port0_reannounce_skipped_outside_the_mask)
{
    reset_state();
    ROUTE_$PORT_ARRAY[0].active = 1;
    ROUTE_$PORT_ARRAY[0].network = 0x11223344;
    call_service();

    ASSERT_EQ(0, rip_update_calls);
    ASSERT_EQ(0, ROUTE_$PORT_ARRAY[0].xns_addr.network);
}

/* ==========================================================================
 * Port lookup / creation
 * ========================================================================== */

/* 0x00E6A1B0-0x00E6A1EA: an unknown port unlocks first, then reports. */
TEST(unknown_port_reports_and_returns)
{
    reset_state();
    find_port_result = -1;
    request.port_type = 2;
    request.socket = 0x1234;
    call_service();

    ASSERT_EQ(1, find_port_calls);
    ASSERT_EQ(2, find_port_net_seen);
    ASSERT_EQ(0x1234, find_port_sock_seen);
    ASSERT_EQ(status_$internet_unknown_network_port, call_status);
    ASSERT_EQ(1, excl_stop_calls);
    ASSERT_EQ(0, rip_send_calls);
}

/* The socket word is sign-extended before the search (0x00E6A1BA "ext.l"). */
TEST(find_port_socket_is_sign_extended)
{
    reset_state();
    request.port_type = 2;
    request.socket = 0xFFFE;
    call_service();
    ASSERT_EQ(-2, find_port_sock_seen);
}

/* 0x00E6A12A-0x00E6A1AE: creation, driver choice and the user-port bookkeeping. */
TEST(create_port_path)
{
    reset_state();
    op_word = SERVICE_OP_CREATE_PORT;
    request.port_type = 1;
    call_service();
    ASSERT_EQ(1, create_port_calls);
    ASSERT_EQ(0, create_port_unit_seen);
    ASSERT_EQ(10, create_port_qlen_seen);           /* the default */
    ASSERT_EQ((uintptr_t)NET_IO_$NIL_DRIVER, (uintptr_t)create_port_driver_seen);
    ASSERT_EQ(0, ROUTE_$N_USER_PORTS);
    ASSERT_EQ(0, wire_calls);

    reset_state();
    op_word = SERVICE_OP_CREATE_PORT;
    request.port_type = 2;
    call_service();
    ASSERT_EQ((uintptr_t)NET_IO_$USER_DRIVER, (uintptr_t)create_port_driver_seen);
    ASSERT_EQ(1, ROUTE_$N_USER_PORTS);
    ASSERT_EQ(1, wire_calls);

    /* A failing create unlocks and returns before the counters move. */
    reset_state();
    op_word = SERVICE_OP_CREATE_PORT;
    request.port_type = 2;
    create_port_status = 0x002B0005;
    call_service();
    ASSERT_EQ(0x002B0005, call_status);
    ASSERT_EQ(0, ROUTE_$N_USER_PORTS);
    ASSERT_EQ(0, rip_send_calls);
}

/* ==========================================================================
 * Status validation and the zero-network rule
 * ========================================================================== */

/* 0x00E6A1EC-0x00E6A21A: statuses outside 1..5 are rejected. */
TEST(status_range_check)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    request.status = 6;
    call_service();
    ASSERT_EQ(status_$route_service_type_bad, call_status);

    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    request.status = 0;
    call_service();
    ASSERT_EQ(status_$route_service_type_bad, call_status);

    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    request.status = 5;
    ROUTE_$PORT_ARRAY[1].network = 1;
    attach_driver(1);
    call_service();
    ASSERT_EQ(status_$ok, call_status);
}

/*
 * 0x00E6A242-0x00E6A272: a zero effective network plus a status in {3,4,5}
 * reports and writes the reply record before unlocking.
 */
TEST(zero_network_needs_a_non_routing_status)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    request.status = 3;
    ROUTE_$PORT_ARRAY[1].network = 0;
    call_service();

    ASSERT_EQ(status_$route_no_routing_zero_network, call_status);
    /* The reply record was filled from the port on the way out. */
    ASSERT_EQ(1, short_port_calls);
    ASSERT_EQ((uintptr_t)&request, (uintptr_t)short_port_dst_seen[0]);
    ASSERT_EQ(0, rip_send_calls);

    /* A status of 1 or 2 is allowed with a zero network. */
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    request.status = 2;
    ROUTE_$PORT_ARRAY[1].network = 0;
    call_service();
    ASSERT_EQ(status_$ok, call_status);
}

/* ==========================================================================
 * The network-change arm
 * ========================================================================== */

/*
 * 0x00E6A29C-0x00E6A390.  Three pairs, and only the middle one carries the
 * 0x0010 hop-count cell at 0x00E69FB0.
 */
TEST(network_change_hop_count_cells)
{
    reset_state();
    op_word = SERVICE_OP_SET_NETWORK;
    ROUTE_$PORT_ARRAY[1].network = 0x0A0A0A0A;   /* old, non-zero */
    request.network = 0x0B0B0B0B;                /* new, non-zero */
    find_port_result = 1;

    call_service();

    /* Removal pair then addition pair - port 0 is idle, so four in all. */
    ASSERT_EQ(4, rip_update_calls);
    ASSERT_EQ(0x0010, rip_hop_value[0]);
    ASSERT_EQ(0x0010, rip_hop_value[1]);
    ASSERT_EQ(0x0000, rip_hop_value[2]);
    ASSERT_EQ(0x0000, rip_hop_value[3]);

    /* All four take a local status, not the caller's. */
    ASSERT_EQ(0, (uintptr_t)rip_status_seen[0] == (uintptr_t)&call_status);
    ASSERT_EQ(0, (uintptr_t)rip_status_seen[3] == (uintptr_t)&call_status);

    /* The new network is stored, and mirrored into the XNS endpoint. */
    ASSERT_EQ(0x0B0B0B0B, ROUTE_$PORT_ARRAY[1].network);
    ASSERT_EQ(0x0B0B0B0B, ROUTE_$PORT_ARRAY[1].xns_addr.network);
}

/* An old network of zero skips the removal pair entirely. */
TEST(network_change_from_zero_skips_the_removal_pair)
{
    reset_state();
    op_word = SERVICE_OP_SET_NETWORK;
    ROUTE_$PORT_ARRAY[1].network = 0;
    request.network = 0x0B0B0B0B;

    call_service();

    ASSERT_EQ(2, rip_update_calls);
    ASSERT_EQ(0x0000, rip_hop_value[0]);
    ASSERT_EQ(0x0000, rip_hop_value[1]);
}

/* 0x00E6A2FE-0x00E6A31A: only port 0 announces and adds the hint. */
TEST(network_change_announces_for_port_zero_only)
{
    reset_state();
    op_word = SERVICE_OP_SET_NETWORK;
    find_port_result = 0;
    ROUTE_$PORT_ARRAY[0].network = 0;
    request.network = 0x0C0C0C0C;
    call_service();
    ASSERT_EQ(1, announce_calls);
    ASSERT_EQ(0x0C0C0C0C, announce_net_seen);
    ASSERT_EQ(1, hint_add_net_calls);
    ASSERT_EQ(0x0C0C0C0C, hint_add_net_seen);

    reset_state();
    op_word = SERVICE_OP_SET_NETWORK;
    find_port_result = 2;
    request.network = 0x0C0C0C0C;
    call_service();
    ASSERT_EQ(0, announce_calls);
    ASSERT_EQ(0, hint_add_net_calls);
}

/* ==========================================================================
 * The status-change arm
 * ========================================================================== */

/* 0x00E6A3B4-0x00E6A3FA: the two routing-counter decrements. */
TEST(status_change_decrements)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 4;      /* in 0x30 and in 0x28? 4 -> only 0x30 */
    request.status = 2;                   /* in 0x0E and in 0x16 */
    attach_driver(1);

    call_service();

    /* Only the standard decrement fires: 4 is in 0x30 but not in 0x28. */
    ASSERT_EQ(1, decrement_calls);
    ASSERT_EQ(0, decrement_a1[0]);
    ASSERT_EQ(1, decrement_a2[0]);
    ASSERT_EQ((int8_t)0xFF, decrement_a3[0]);

    /* Status 5 is in both masks, and 1 is in both target masks. */
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 5;
    request.status = 1;
    attach_driver(1);
    call_service();
    ASSERT_EQ(2, decrement_calls);
    ASSERT_EQ((int8_t)0xFF, decrement_a3[0]);
    ASSERT_EQ(0, decrement_a3[1]);
}

/*
 * 0x00E6A3FC-0x00E6A446: leaving status 1 runs the driver's leave entry and
 * then its attach entry with five arguments.
 */
TEST(leaving_status_one_runs_both_driver_entries)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 1;
    ROUTE_$PORT_ARRAY[1].socket = 0x2468;
    request.status = 2;
    attach_driver(1);
    test_driver.leave_status_1 = ARCH_PTR_TO_VA(test_leave_fn);
    test_driver.attach_service = ARCH_PTR_TO_VA(test_attach_fn);

    call_service();

    ASSERT_EQ(1, leave_calls);
    ASSERT_EQ((uintptr_t)&ROUTE_$PORT_ARRAY[1].socket,
              (uintptr_t)leave_socket_seen);

    ASSERT_EQ(1, attach_calls);
    ASSERT_EQ((uintptr_t)&ROUTE_$PORT_ARRAY[1].socket,
              (uintptr_t)attach_socket_seen);
    /* The zero record at 0x00E6A02C, the request word 0 and the caller's
     * status pointer. */
    ASSERT_EQ(0, attach_rec_seen[0]);
    ASSERT_EQ(0, attach_rec_seen[1]);
    ASSERT_EQ(0, attach_request_seen);
    ASSERT_EQ((uintptr_t)&call_status, (uintptr_t)attach_out5_seen);
    ASSERT_EQ(1, attach_out4_seen != NULL);
    ASSERT_EQ(1, attach_out4_seen != (void *)&call_status);
}

/* 0x00E6A41E: a failing leave entry suppresses the attach entry. */
TEST(failing_leave_entry_skips_the_attach_entry)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 1;
    request.status = 2;
    attach_driver(1);
    test_driver.leave_status_1 = ARCH_PTR_TO_VA(test_leave_fn);
    test_driver.attach_service = ARCH_PTR_TO_VA(test_attach_fn);
    leave_status = 0x002B0007;

    call_service();

    ASSERT_EQ(1, leave_calls);
    ASSERT_EQ(0, attach_calls);
}

/* 0x00E6A452-0x00E6A4AC: the IDP registration only runs on a clean leave. */
TEST(idp_registration_on_leaving_status_one)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 1;
    request.status = 2;
    attach_driver(1);
    RIP_$STD_IDP_CHANNEL = 3;
    APP_$STD_IDP_CHANNEL = 4;

    call_service();

    ASSERT_EQ(2, idp_add_calls);
    ASSERT_EQ((uintptr_t)&RIP_$STD_IDP_CHANNEL, (uintptr_t)idp_add_chan[0]);
    ASSERT_EQ(1, idp_add_port[0]);
    ASSERT_EQ((uintptr_t)&APP_$STD_IDP_CHANNEL, (uintptr_t)idp_add_chan[1]);
    ASSERT_EQ(1, idp_add_port[1]);

    /* A channel of -1 is skipped. */
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 1;
    request.status = 2;
    attach_driver(1);
    RIP_$STD_IDP_CHANNEL = -1;
    APP_$STD_IDP_CHANNEL = 0xFFFF;
    call_service();
    ASSERT_EQ(0, idp_add_calls);
}

/*
 * 0x00E6A4AE-0x00E6A4EC: entering a routing status needs the standard IDP
 * channel; without one the routine reports "network port not open".
 */
TEST(entering_routing_needs_the_std_channel)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 2;      /* in 0x0E */
    request.status = 4;                   /* in 0x30 */
    attach_driver(1);
    RIP_$STD_IDP_CHANNEL = 3;

    call_service();

    ASSERT_EQ(1, init_routing_calls);
    ASSERT_EQ(1, init_routing_port[0]);
    ASSERT_EQ((int8_t)0xFF, init_routing_type[0]);
    ASSERT_EQ(status_$ok, call_status);

    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 2;
    request.status = 4;
    attach_driver(1);
    RIP_$STD_IDP_CHANNEL = -1;

    call_service();

    ASSERT_EQ(0, init_routing_calls);
    ASSERT_EQ(status_$internet_network_port_not_open, call_status);
}

/* 0x00E6A4EE-0x00E6A516: the non-standard initialisation has no channel test. */
TEST(entering_n_routing_has_no_channel_test)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 2;      /* in 0x16 */
    request.status = 3;                   /* in 0x28, not in 0x30 */
    attach_driver(1);
    RIP_$STD_IDP_CHANNEL = -1;

    call_service();

    ASSERT_EQ(1, init_routing_calls);
    ASSERT_EQ(1, init_routing_port[0]);
    ASSERT_EQ(0, init_routing_type[0]);
    ASSERT_EQ(status_$ok, call_status);
}

/* ==========================================================================
 * The status-restore rule (0x00E6A518-0x00E6A594)
 * ========================================================================== */

/*
 * "tst.l (A0) / bne.b 0x00E6A590": a non-zero status restores the old one.
 */
TEST(restore_only_on_a_non_zero_status)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 2;
    request.status = 4;
    attach_driver(1);
    RIP_$STD_IDP_CHANNEL = -1;            /* forces status 0x2B0001 */

    call_service();

    ASSERT_EQ(status_$internet_network_port_not_open, call_status);
    /* 0x00E6A590 put the old status back. */
    ASSERT_EQ(2, ROUTE_$PORT_ARRAY[1].active);
    /* The close-side cleanup never ran. */
    ASSERT_EQ(0, enter_calls);
    ASSERT_EQ(0, idp_del_calls);
}

/*
 * The defect this test pins: "cmpi.w #0x1,(0x2c,A2) / bne.b 0x00E6A596".
 * With a zero status and a new status that is not 1, the image falls straight
 * through to the unlock - it does NOT restore the old status.
 */
TEST(no_restore_when_status_is_ok_and_new_status_is_not_one)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 2;
    request.status = 3;
    attach_driver(1);

    call_service();

    ASSERT_EQ(status_$ok, call_status);
    /* The new status stands. */
    ASSERT_EQ(3, ROUTE_$PORT_ARRAY[1].active);
    /* And the close-side cleanup did not run either. */
    ASSERT_EQ(0, enter_calls);
    ASSERT_EQ(0, idp_del_calls);
}

/*
 * The third outcome: a zero status and a new status of 1 runs the driver's
 * enter entry and unregisters the port from both IDP channels.
 */
TEST(entering_status_one_runs_the_close_side_cleanup)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 2;
    ROUTE_$PORT_ARRAY[1].socket = 0x1357;
    request.status = 1;
    attach_driver(1);
    test_driver.enter_status_1 = ARCH_PTR_TO_VA(test_enter_fn);
    RIP_$STD_IDP_CHANNEL = 3;
    APP_$STD_IDP_CHANNEL = 4;

    call_service();

    ASSERT_EQ(status_$ok, call_status);
    ASSERT_EQ(1, ROUTE_$PORT_ARRAY[1].active);
    ASSERT_EQ(1, enter_calls);
    ASSERT_EQ((uintptr_t)&ROUTE_$PORT_ARRAY[1].socket,
              (uintptr_t)enter_socket_seen);
    ASSERT_EQ(2, idp_del_calls);
    ASSERT_EQ((uintptr_t)&RIP_$STD_IDP_CHANNEL, (uintptr_t)idp_del_chan[0]);
    ASSERT_EQ(1, idp_del_port[0]);
    ASSERT_EQ((uintptr_t)&APP_$STD_IDP_CHANNEL, (uintptr_t)idp_del_chan[1]);
}

/* An unchanged status skips the whole arm (0x00E6A3A8 "beq.w 0x00E6A596"). */
TEST(unchanged_status_skips_the_arm)
{
    reset_state();
    op_word = SERVICE_OP_SET_STATUS;
    ROUTE_$PORT_ARRAY[1].network = 1;
    ROUTE_$PORT_ARRAY[1].active = 3;
    request.status = 3;
    attach_driver(1);

    call_service();

    ASSERT_EQ(0, decrement_calls);
    ASSERT_EQ(0, init_routing_calls);
    ASSERT_EQ(0, idp_add_calls);
    ASSERT_EQ(3, ROUTE_$PORT_ARRAY[1].active);
    /* The tail still runs. */
    ASSERT_EQ(2, rip_send_calls);
}

/* ==========================================================================
 * The tail
 * ========================================================================== */

/*
 * 0x00E6A596-0x00E6A5CA: unlock, then RIP_$SEND_UPDATES(0) and
 * RIP_$SEND_UPDATES(0xFF), then the reply record.
 */
TEST(tail_order_and_arguments)
{
    reset_state();
    find_port_result = 2;
    ROUTE_$PORT_ARRAY[2].network = 0x77777777;
    ROUTE_$PORT_ARRAY[2].active = 3;
    ROUTE_$PORT_ARRAY[2].port_type = 2;
    ROUTE_$PORT_ARRAY[2].socket = 0x0505;
    ROUTE_$PORT_ARRAY[2].socket2 = 0x0606;

    call_service();

    ASSERT_EQ(1, excl_stop_calls);
    ASSERT_EQ(2, rip_send_calls);
    ASSERT_EQ(0, rip_send_arg[0]);
    ASSERT_EQ((int8_t)0xFF, rip_send_arg[1]);

    /* The reply is taken from the port the request selected. */
    ASSERT_EQ(1, short_port_calls);
    ASSERT_EQ((uintptr_t)&ROUTE_$PORT_ARRAY[2],
              (uintptr_t)short_port_src_seen[0]);
    ASSERT_EQ((uintptr_t)&request, (uintptr_t)short_port_dst_seen[0]);
    ASSERT_EQ(0x77777777, request.network);
    ASSERT_EQ(3, request.status);
    ASSERT_EQ(2, request.port_type);
    ASSERT_EQ(0x0505, request.socket);
    ASSERT_EQ(0x0606, request.queue_length);
}

int main(void)
{
    printf("ROUTE_$SERVICE tests\n");

    RUN_TEST(close_port_bit_returns_early);
    RUN_TEST(close_port_uses_the_parents_frame);
    RUN_TEST(user_port_validation);
    RUN_TEST(port0_reannounce_stores_xns_network);
    RUN_TEST(port0_reannounce_skipped_outside_the_mask);
    RUN_TEST(unknown_port_reports_and_returns);
    RUN_TEST(find_port_socket_is_sign_extended);
    RUN_TEST(create_port_path);
    RUN_TEST(status_range_check);
    RUN_TEST(zero_network_needs_a_non_routing_status);
    RUN_TEST(network_change_hop_count_cells);
    RUN_TEST(network_change_from_zero_skips_the_removal_pair);
    RUN_TEST(network_change_announces_for_port_zero_only);
    RUN_TEST(status_change_decrements);
    RUN_TEST(leaving_status_one_runs_both_driver_entries);
    RUN_TEST(failing_leave_entry_skips_the_attach_entry);
    RUN_TEST(idp_registration_on_leaving_status_one);
    RUN_TEST(entering_routing_needs_the_std_channel);
    RUN_TEST(entering_n_routing_has_no_channel_test);
    RUN_TEST(restore_only_on_a_non_zero_status);
    RUN_TEST(no_restore_when_status_is_ok_and_new_status_is_not_one);
    RUN_TEST(entering_status_one_runs_the_close_side_cleanup);
    RUN_TEST(unchanged_status_skips_the_arm);
    RUN_TEST(tail_order_and_arguments);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
