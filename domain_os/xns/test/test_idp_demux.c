/*
 * xns/test/test_idp_demux.c - Unit tests for XNS_IDP_$OS_DEMUX (0x00E184A8)
 * and XNS_IDP_$DEMUX (0x00E18B8A).
 *
 * xns/idp_demux.c is #included below, so the code under test is the real
 * thing; every callee is stubbed here and records what it was handed.
 *
 * The behaviours covered are the ones the 2026-09-06 audit found wrong:
 *   - the two parameter records are real structures based at A6-0x88 and
 *     A6-0x40, so XNS_ERROR_$SEND and the channel callback see initialised
 *     fields at +0x18/+0x1C rather than uninitialised stack
 *   - a successful SOCK_$PUT returns with status_$ok (0x00E1870C)
 *   - one drop is counted per dropped packet, and the callback's status is
 *     not clobbered
 *   - the two DISTINCT error constants 0x0001 (0x00E1872A) and 0x0201
 *     (0x00E18728)
 *   - the forwarding record's clr.w at +0x12
 *   - mac_info copies are 16 bytes, not 32
 *   - the two drop counters at 0x00E87FB4 and 0x00E87FB0
 *   - the channel flags word is stored into the socket record
 *   - the word read at descriptor +0x1E
 */

#include <stdio.h>
#include <string.h>

#include "xns/xns_internal.h"
#include "route/route.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed;

#define RUN_TEST(name) do {                      \
    printf("  Running %s... ", #name);           \
    current_failed = 0;                          \
    setup();                                     \
    test_##name();                               \
    if (current_failed) { tests_failed++; }      \
    else { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    unsigned long _e = (unsigned long)(expected);                             \
    unsigned long _a = (unsigned long)(actual);                               \
    if (_e != _a) {                                                           \
        if (!current_failed) printf("FAILED\n");                              \
        printf("    line %d: expected 0x%lx, got 0x%lx\n", __LINE__, _e, _a); \
        current_failed = 1;                                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#define ASSERT_PTR(expected, actual) do {                                     \
    const void *_e = (const void *)(expected);                                \
    const void *_a = (const void *)(actual);                                  \
    if (_e != _a) {                                                           \
        if (!current_failed) printf("FAILED\n");                              \
        printf("    line %d: expected %p, got %p\n", __LINE__, _e, _a);       \
        current_failed = 1;                                                   \
        return;                                                               \
    }                                                                         \
} while (0)

/* ============================================================================
 * Globals the demux path touches
 * ============================================================================ */

static xns_$idp_state_t idp_state;
uint8_t *XNS_IDP_BASE = (uint8_t *)&idp_state;

static route_$port_t    port0;
route_$port_t          *ROUTE_$PORTP[8];
uint16_t                ROUTE_$SOCK;
int16_t                 ROUTE_$STD_N_ROUTING_PORTS;
uint32_t                ROUTE_$STD_TOO_FAR;
uint32_t                ROUTE_$STD_MISROUTE;

/* ============================================================================
 * Recorded call state
 * ============================================================================ */

static int      chksum_calls;
static void    *chksum_arg;
static int16_t  chksum_result;

static int      is_bcast_calls;
static void    *is_bcast_arg;
static int8_t   is_bcast_result;

static int      err_send_calls;
static void    *err_send_pkt;
static uint16_t err_send_code;
static uint16_t *err_send_code_p;
static uint16_t err_send_param;

static int      sock_put_calls;
static uint16_t sock_put_sock;
static void    *sock_put_pkt;
static uint8_t  sock_put_flags;
static uint16_t sock_put_p4;
static uint16_t sock_put_p5;
static int8_t   sock_put_result;
static xns_$sock_pkt_t sock_put_copy;

static int      demux_calls;
static xns_$pkt_desc_t demux_rec_copy;
static uint16_t *demux_port_type_p;
static uint16_t *demux_port_socket_p;
static boolean  *demux_bcast_p;
static status_$t demux_set_status;

/* ============================================================================
 * Stubs
 * ============================================================================ */

int16_t xns_$get_checksum(void *packet_info)
{
    chksum_calls++;
    chksum_arg = packet_info;
    return chksum_result;
}

int8_t xns_$is_broadcast_addr(void *addr)
{
    is_bcast_calls++;
    is_bcast_arg = addr;
    return is_bcast_result;
}

void XNS_ERROR_$SEND(void *packet_info, uint16_t *error_code,
                     uint16_t *error_param, uint16_t *result_ret,
                     status_$t *status_ret)
{
    err_send_calls++;
    err_send_pkt = packet_info;
    err_send_code_p = error_code;
    err_send_code = *error_code;
    err_send_param = *error_param;
    *result_ret = 0;
    *status_ret = status_$ok;
}

int8_t SOCK_$PUT(uint16_t sock_num, sock_$pkt_info_t *pkt_info, int8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2)
{
    sock_put_calls++;
    sock_put_sock = sock_num;
    sock_put_pkt = (void **)pkt_info;
    sock_put_flags = (uint8_t)flags;
    sock_put_p4 = ec_param1;
    sock_put_p5 = ec_param2;
    memcpy(&sock_put_copy, pkt_info, sizeof(sock_put_copy));
    return sock_put_result;
}

void xns_$add_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    (void)channel; (void)port; *status_ret = status_$ok;
}

void xns_$delete_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    (void)channel; (void)port; *status_ret = status_$ok;
}

void ML_$EXCLUSION_START(ml_$exclusion_t *excl) { (void)excl; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)  { (void)excl; }

static void test_demux_vector(xns_$pkt_desc_t *rec, uint16_t *port_type,
                              uint16_t *port_socket, boolean *mac_broadcast,
                              status_$t *status_ret)
{
    demux_calls++;
    memcpy(&demux_rec_copy, rec, sizeof(demux_rec_copy));
    demux_port_type_p = port_type;
    demux_port_socket_p = port_socket;
    demux_bcast_p = mac_broadcast;
    *status_ret = demux_set_status;
}

/* The functions under test. */
#include "xns/idp_demux.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static xns_$mac_rcv_t   pkt;
static xns_$idp_header_t header;
static int16_t          the_port = 0;
static boolean          mac_bcast;
static status_$t        st;

/*
 * The two packet records now carry target virtual addresses rather than C
 * pointers (bead source-ronb), so the arena base has to sit below every
 * object the tests round-trip through them.
 */
static void va_base_setup(void)
{
    uintptr_t lo = (uintptr_t)&header;

    if ((uintptr_t)&idp_state < lo) lo = (uintptr_t)&idp_state;
    if ((uintptr_t)&pkt < lo)       lo = (uintptr_t)&pkt;
    if ((uintptr_t)&port0 < lo)     lo = (uintptr_t)&port0;
    ARCH_HOST_VA_BASE = lo - 0x10000u;
}

static void setup(void)
{
    int i;

    va_base_setup();
    memset(&idp_state, 0, sizeof(idp_state));
    memset(&pkt, 0, sizeof(pkt));
    memset(&header, 0, sizeof(header));
    memset(&port0, 0, sizeof(port0));
    memset(ROUTE_$PORTP, 0, sizeof(ROUTE_$PORTP));
    memset(&sock_put_copy, 0xAA, sizeof(sock_put_copy));
    memset(&demux_rec_copy, 0xAA, sizeof(demux_rec_copy));

    ROUTE_$PORTP[0] = &port0;
    port0.port_type = 0x1111;
    port0.socket = 0x2222;
    ROUTE_$SOCK = 0x0077;
    ROUTE_$STD_N_ROUTING_PORTS = 2;
    ROUTE_$STD_TOO_FAR = 0;
    ROUTE_$STD_MISROUTE = 0;

    chksum_calls = 0; chksum_arg = NULL; chksum_result = 0x1234;
    is_bcast_calls = 0; is_bcast_arg = NULL; is_bcast_result = -1;
    err_send_calls = 0; err_send_code = 0xFFFF; err_send_param = 0xFFFF;
    sock_put_calls = 0; sock_put_result = -1;
    demux_calls = 0; demux_set_status = status_$ok;

    /* A well formed header: no checksum, our socket, one hop used. */
    header.checksum = 0xFFFF;
    header.transport_ctl = 1;
    header.dest_socket = 0x0050;
    for (i = 0; i < 6; i++) {
        header.src_host[i] = (uint8_t)(0x10 + i);
        header.dest_host[i] = (uint8_t)(0x20 + i);
    }

    pkt.d.data_len = 0x00110022;
    pkt.d.header = ARCH_PTR_TO_VA(&header);
    pkt.d.iov = 0x33445566;
    pkt.d.mac_src_hi = 0x778899AA;
    pkt.d.mac_src_lo = 0xBBCC;
    pkt.d.pkt_len = 0xDDEE;
    pkt.d._unknown_2e = 0xFF01;
    pkt.d._unknown_34 = 0x5A5B;
    pkt.d.port_info = 0x0A0B;
    for (i = 0; i < 0x10; i++) {
        pkt.d.mac_info[i] = (uint8_t)(0xC0 + i);
    }

    /*
     * One channel bound to the destination socket with a demux vector.
     *
     * The channel table is addressed through XNS_CHANNEL_PTR(), which walks
     * the state block with the m68k stride (0x48).  The host's
     * xns_$channel_t is wider than that, so the fixture must go through the
     * same accessor the code under test uses rather than through the
     * xns_$idp_state_t.channels[] array.
     */
    XNS_CHANNEL_PTR(3)->xns_socket = 0x0050;
    XNS_CHANNEL_PTR(3)->demux = (code_ptr_t)test_demux_vector;
    XNS_CHANNEL_PTR(3)->user_socket = 0x0060;

    the_port = 0;
    mac_bcast = false;
    st = 0x7E7E7E7E;
}

static void run(void)
{
    XNS_IDP_$OS_DEMUX(&pkt, (int16_t *)&the_port, &mac_bcast, &st);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* Every packet is counted on the way in. */
static void test_packet_counted(void)
{
    run();
    ASSERT_EQ(1, idp_state.packets_received);
}

/* A broadcast SOURCE host is dropped once with status_$xns_no_client_for_packet. */
static void test_broadcast_source_dropped(void)
{
    int i;
    for (i = 0; i < 6; i++) {
        header.src_host[i] = 0xFF;
    }
    run();
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_no_client_for_packet, st);
    ASSERT_EQ(0, chksum_calls);
    ASSERT_EQ(0, demux_calls);
}

/* checksum == 0xFFFF means "not checksummed": no checksum is computed. */
static void test_no_checksum_field(void)
{
    run();
    ASSERT_EQ(0, chksum_calls);
    ASSERT_EQ(1, demux_calls);
}

/*
 * A bad checksum on a packet addressed to us reports error 0x0001 with
 * parameter 0x0000, counts ONE drop and returns status_$xns_bad_checksum.
 */
static void test_bad_checksum_at_destination(void)
{
    header.checksum = 0x1111;
    chksum_result = 0x2222;
    is_bcast_result = -1;               /* the address is ours/broadcast */
    run();
    ASSERT_EQ(1, chksum_calls);
    ASSERT_PTR(&pkt, chksum_arg);
    ASSERT_EQ(1, err_send_calls);
    ASSERT_EQ(0x0001, err_send_code);
    ASSERT_EQ(0x0000, err_send_param);
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_bad_checksum, st);
    ASSERT_EQ(0, demux_calls);
}

/* The same packet only passing through reports the DIFFERENT code 0x0201. */
static void test_bad_checksum_in_transit(void)
{
    header.checksum = 0x1111;
    chksum_result = 0x2222;
    is_bcast_result = 0;                /* not ours */
    run();
    ASSERT_EQ(1, err_send_calls);
    ASSERT_EQ(0x0201, err_send_code);
    ASSERT_EQ(0x0000, err_send_param);
    ASSERT_EQ(status_$xns_bad_checksum, st);
}

/* The two error constants really are two separate cells. */
static void test_error_constants_are_distinct(void)
{
    ASSERT_EQ(0x0000, xns_idp_c_error_param_none);
    ASSERT_EQ(0x0201, xns_idp_c_bad_checksum_transit);
    ASSERT_EQ(0x0001, xns_idp_c_bad_checksum_at_dest);
}

/* A matching checksum is not an error. */
static void test_good_checksum(void)
{
    header.checksum = 0x1111;
    chksum_result = 0x1111;
    run();
    ASSERT_EQ(1, chksum_calls);
    ASSERT_EQ(0, err_send_calls);
    ASSERT_EQ(1, demux_calls);
}

/*
 * The record handed to XNS_ERROR_$SEND is a real structure: the callee reads
 * +0x18 and +0x1C, which must hold the descriptor's data_len and header.
 */
static void test_error_record_is_initialised(void)
{
    header.checksum = 0x1111;
    chksum_result = 0x2222;
    run();
    ASSERT_EQ(1, err_send_calls);
    ASSERT_EQ(0x00110022, ((xns_$pkt_desc_t *)err_send_pkt)->data_len);
    ASSERT_PTR(&header,
               ARCH_VA_TO_PTR(((xns_$pkt_desc_t *)err_send_pkt)->header));
    ASSERT_EQ((int8_t)-1, ((xns_$pkt_desc_t *)err_send_pkt)->from_net);
}

/* Local delivery hands the channel callback a fully built record. */
static void test_local_delivery_record(void)
{
    int i;
    run();
    ASSERT_EQ(1, demux_calls);
    ASSERT_EQ(0x00110022, demux_rec_copy.data_len);
    ASSERT_PTR(&header, ARCH_VA_TO_PTR(demux_rec_copy.header));
    ASSERT_EQ(0x33445566, demux_rec_copy.iov);
    ASSERT_EQ((int8_t)-1, demux_rec_copy.from_net);
    ASSERT_EQ(0x778899AA, demux_rec_copy.mac_src_hi);
    ASSERT_EQ(0xBBCC, demux_rec_copy.mac_src_lo);
    ASSERT_PTR(XNS_CHANNEL_PTR(3), ARCH_VA_TO_PTR(demux_rec_copy.channel));
    ASSERT_EQ(0x0A0B, demux_rec_copy.port_info);
    ASSERT_EQ(0x5A5B, demux_rec_copy._unknown_34);
    for (i = 0; i < 0x10; i++) {
        ASSERT_EQ(0xC0 + i, demux_rec_copy.mac_info[i]);
    }
    /* the port record fields are passed by address, not by value */
    ASSERT_PTR(&port0.port_type, demux_port_type_p);
    ASSERT_PTR(&port0.socket, demux_port_socket_p);
    ASSERT_PTR(&mac_bcast, demux_bcast_p);
    /* the callback succeeded: no drop, status untouched */
    ASSERT_EQ(0, idp_state.packets_dropped);
    ASSERT_EQ(status_$ok, st);
}

/*
 * A failing callback counts exactly ONE drop and its status survives - the
 * audit found the status clobbered and the counter bumped twice.
 */
static void test_callback_failure_preserves_status(void)
{
    demux_set_status = 0x00AB00CD;
    run();
    ASSERT_EQ(1, demux_calls);
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(0x00AB00CD, st);
}

/* Socket 0 and socket 0xFFFF are not deliverable. */
static void test_undeliverable_sockets(void)
{
    header.dest_socket = 0;
    run();
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_no_client_for_packet, st);
    ASSERT_EQ(0, demux_calls);

    setup();
    header.dest_socket = 0xFFFF;
    run();
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_no_client_for_packet, st);
    ASSERT_EQ(0, demux_calls);
}

/* No channel bound to the socket: one drop, no_route. */
static void test_no_channel_bound(void)
{
    XNS_CHANNEL_PTR(3)->xns_socket = 0x0051;
    run();
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_no_client_for_packet, st);
    ASSERT_EQ(0, demux_calls);
}

/* A channel with no demux vector installed is the same as no channel. */
static void test_channel_without_vector(void)
{
    XNS_CHANNEL_PTR(3)->demux = NULL;
    run();
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_no_client_for_packet, st);
    ASSERT_EQ(0, demux_calls);
}

/* Not for us and this node does not route: 0x00E87FB4 is bumped. */
static void test_forward_not_routing(void)
{
    is_bcast_result = 0;
    ROUTE_$STD_N_ROUTING_PORTS = 1;
    run();
    ASSERT_EQ(1, ROUTE_$STD_MISROUTE);
    ASSERT_EQ(0, ROUTE_$STD_TOO_FAR);
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_no_client_for_packet, st);
    ASSERT_EQ(0, sock_put_calls);
}

/* Out of hops: 0x00E87FB0 is bumped and the status is hop_count_exceeded. */
static void test_forward_hop_limit(void)
{
    is_bcast_result = 0;
    header.transport_ctl = 15;
    run();
    ASSERT_EQ(1, ROUTE_$STD_TOO_FAR);
    ASSERT_EQ(0, ROUTE_$STD_MISROUTE);
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_hop_count_exceeded, st);
    ASSERT_EQ(0, sock_put_calls);
}

/* The transport control byte is compared UNSIGNED against 15. */
static void test_forward_hop_limit_unsigned(void)
{
    is_bcast_result = 0;
    header.transport_ctl = 0x80;    /* negative as a signed byte, but >= 15 */
    run();
    ASSERT_EQ(1, ROUTE_$STD_TOO_FAR);
    ASSERT_EQ(status_$xns_hop_count_exceeded, st);
}

/*
 * A successful SOCK_$PUT leaves *status_ret at status_$ok and counts no drop
 * (0x00E1870C) - the audit found the success path falling into the failure
 * path.
 */
static void test_forward_success(void)
{
    is_bcast_result = 0;
    sock_put_result = (int8_t)-1;
    run();
    ASSERT_EQ(1, sock_put_calls);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, idp_state.packets_dropped);
    ASSERT_EQ(0x0077, sock_put_sock);
    ASSERT_EQ(0, sock_put_flags);
    ASSERT_EQ(0x1111, sock_put_p4);     /* port_type */
    ASSERT_EQ(0x2222, sock_put_p5);     /* socket */
}

/* A refused SOCK_$PUT counts one drop and reports packet_dropped. */
static void test_forward_failure(void)
{
    is_bcast_result = 0;
    sock_put_result = 0;
    run();
    ASSERT_EQ(1, sock_put_calls);
    ASSERT_EQ(1, idp_state.packets_dropped);
    ASSERT_EQ(status_$xns_packet_dropped, st);
}

/*
 * The forwarding record: flags word, the longword read that spans +0x2C and
 * +0x2E, the WORD read of +0x1E, the clr.w at +0x12 and the 16-byte mac_info
 * copy.
 */
static void test_forward_record_contents(void)
{
    int i;
    is_bcast_result = 0;
    run();
    ASSERT_EQ(1, sock_put_calls);
    ASSERT_EQ(XNS_SOCK_PKT_F_IDP, sock_put_copy.flags);
    ASSERT_PTR(&header, ARCH_VA_TO_PTR(sock_put_copy.header));
    ASSERT_EQ(0x778899AA, sock_put_copy.mac_src_hi);
    ASSERT_EQ(0xBBCC, sock_put_copy.mac_src_lo);
    /* the longword at descriptor +0x2C: pkt_len in the high half */
    ASSERT_EQ(0xDDEEFF01u, sock_put_copy.data_len);
    /* the word at descriptor +0x1E: the low half of data_len (0x00110022) */
    ASSERT_EQ(0x0022, sock_put_copy.header_len);
    ASSERT_EQ(0x0A0B, sock_put_copy.port_info);
    ASSERT_EQ(0, sock_put_copy.reserved_12);
    for (i = 0; i < 0x10; i++) {
        ASSERT_EQ(0xC0 + i, sock_put_copy.mac_info[i]);
    }
}

/*
 * XNS_IDP_$DEMUX stores the flags word it computes into the socket record -
 * the audit found it computed and then discarded.
 */
static void test_channel_demux_stores_flags(void)
{
    xns_$pkt_desc_t rec;
    /* static so its address is in the same arena as everything else the VA
     * round trip covers (a stack address need not be within 4GB of it) */
    static xns_$channel_t chan;
    uint16_t port_type = 0x3333;
    uint16_t port_socket = 0x4444;
    boolean bcast;
    status_$t s = 0x7E7E7E7E;
    int i;

    memset(&rec, 0, sizeof(rec));
    memset(&chan, 0, sizeof(chan));
    va_base_setup();
    rec.header = ARCH_PTR_TO_VA(&header);
    rec.channel = ARCH_PTR_TO_VA(&chan);
    rec.data_len = 0x00110022;
    rec.pkt_len = 0xDDEE;
    rec.mac_src_hi = 0x778899AA;
    rec.mac_src_lo = 0xBBCC;
    rec.port_info = 0x0A0B;
    for (i = 0; i < 0x10; i++) {
        rec.mac_info[i] = (uint8_t)(0xC0 + i);
    }
    chan.user_socket = 0x0060;
    for (i = 0; i < 6; i++) {
        header.dest_host[i] = 0xFF;     /* broadcast destination host */
    }
    bcast = true;                       /* and a MAC level broadcast */

    sock_put_calls = 0;
    sock_put_result = (int8_t)-1;
    XNS_IDP_$DEMUX(&rec, &port_type, &port_socket, &bcast, &s);

    ASSERT_EQ(1, sock_put_calls);
    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(0x0060, sock_put_sock);
    ASSERT_EQ(0x3333, sock_put_p4);
    ASSERT_EQ(0x4444, sock_put_p5);
    ASSERT_EQ(XNS_SOCK_PKT_F_IDP | XNS_SOCK_PKT_F_BROADCAST |
              XNS_SOCK_PKT_F_MAC_BCAST, sock_put_copy.flags);
    /* the zero-extended WORD read of rec+0x2C, not a longword */
    ASSERT_EQ(0x0000DDEEu, sock_put_copy.data_len);
    /* the word read of rec+0x1A */
    ASSERT_EQ(0x0022, sock_put_copy.header_len);
    ASSERT_EQ(0, sock_put_copy.reserved_12);
    for (i = 0; i < 0x10; i++) {
        ASSERT_EQ(0xC0 + i, sock_put_copy.mac_info[i]);
    }
}

/* An unbound channel is reported as no_route without counting a drop. */
static void test_channel_demux_unbound(void)
{
    xns_$pkt_desc_t rec;
    /* static so its address is in the same arena as everything else the VA
     * round trip covers (a stack address need not be within 4GB of it) */
    static xns_$channel_t chan;
    uint16_t port_type = 0;
    uint16_t port_socket = 0;
    boolean bcast = false;
    status_$t s = 0x7E7E7E7E;

    memset(&rec, 0, sizeof(rec));
    memset(&chan, 0, sizeof(chan));
    va_base_setup();
    rec.header = ARCH_PTR_TO_VA(&header);
    rec.channel = ARCH_PTR_TO_VA(&chan);
    chan.user_socket = XNS_NO_SOCKET;

    sock_put_calls = 0;
    XNS_IDP_$DEMUX(&rec, &port_type, &port_socket, &bcast, &s);

    ASSERT_EQ(0, sock_put_calls);
    ASSERT_EQ(status_$xns_no_client_for_packet, s);
    ASSERT_EQ(0, idp_state.packets_dropped);
}

int main(void)
{
    printf("Running XNS_IDP_$OS_DEMUX tests...\n");

    RUN_TEST(packet_counted);
    RUN_TEST(broadcast_source_dropped);
    RUN_TEST(no_checksum_field);
    RUN_TEST(bad_checksum_at_destination);
    RUN_TEST(bad_checksum_in_transit);
    RUN_TEST(error_constants_are_distinct);
    RUN_TEST(good_checksum);
    RUN_TEST(error_record_is_initialised);
    RUN_TEST(local_delivery_record);
    RUN_TEST(callback_failure_preserves_status);
    RUN_TEST(undeliverable_sockets);
    RUN_TEST(no_channel_bound);
    RUN_TEST(channel_without_vector);
    RUN_TEST(forward_not_routing);
    RUN_TEST(forward_hop_limit);
    RUN_TEST(forward_hop_limit_unsigned);
    RUN_TEST(forward_success);
    RUN_TEST(forward_failure);
    RUN_TEST(forward_record_contents);
    RUN_TEST(channel_demux_stores_flags);
    RUN_TEST(channel_demux_unbound);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
