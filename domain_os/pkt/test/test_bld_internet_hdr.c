/*
 * pkt/test/test_bld_internet_hdr.c - Unit tests for PKT_$BLD_INTERNET_HDR
 * (0x00E1202C).
 *
 * The test compiles the real pkt/bld_internet_hdr.c and supplies scripted
 * versions of the four things it calls out to (RIP_$FIND_NEXTHOP,
 * OS_$DATA_COPY, the ROUTE port tables and NODE_$ME), so each of the
 * original's paths - loopback, local routing, internet routing to this node,
 * a direct port, a gateway, and every size check - can be driven and the
 * recovered pkt_$hdr_t layout checked field by field.
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
 * Globals and scripted callees
 * ========================================================================== */

#include "pkt/pkt_internal.h"

uint32_t NODE_$ME;
int8_t NETWORK_$LOOPBACK_FLAG;
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];

/* --- RIP_$FIND_NEXTHOP ---------------------------------------------------- */
static int16_t rip_result;
static status_$t rip_status;
static int16_t rip_port;
static uint32_t rip_nexthop_node;
static rip_$dest_addr_t rip_seen_dest;
static boolean rip_seen_flags;
static int rip_calls;

int16_t RIP_$FIND_NEXTHOP(void *addr_info, boolean flags, int16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret)
{
    rip_$nexthop_t nh;

    rip_calls++;
    rip_seen_dest = *(rip_$dest_addr_t *)addr_info;
    rip_seen_flags = flags;

    memset(&nh, 0, sizeof(nh));
    nh.host_lo = rip_nexthop_node;
    memcpy(nexthop_ret, &nh, sizeof(nh));

    *port_ret = rip_port;
    *status_ret = rip_status;
    return rip_result;
}

/* --- OS_$DATA_COPY -------------------------------------------------------- */
static int copy_calls;
static const void *copy_src;
static void *copy_dst;
static uint32_t copy_len;

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    copy_calls++;
    copy_src = src;
    copy_dst = dst;
    copy_len = len;
    memcpy(dst, src, (size_t)len);
}

#include "../bld_internet_hdr.c"

/* ==========================================================================
 * Fixture
 * ========================================================================== */

#define TEST_NODE_ME    0x000ABCDEu
#define TEST_DEST_NODE  0x00012345u
#define TEST_SRC_NODE   0x00054321u
#define TEST_KEY        0x11223344u
#define TEST_PORT       3

/* the header buffer is a whole 1KB netbuf page in the original */
static uint8_t hdr_page[0x400];
static pkt_$hdr_t *hdr = (pkt_$hdr_t *)hdr_page;

static pkt_$info_t info;
static route_$port_t test_port;
static uint8_t template_buf[64];

/*
 * route_$port_t.driver_info is a 32-bit TARGET address, so the driver record
 * has to live somewhere ARCH_VA_TO_PTR can reach.  Point ARCH_HOST_VA_BASE at
 * a local arena and keep the record at a fixed offset in it.
 */
static uint8_t va_arena[0x400];
#define TEST_DRV_VA 0x100
#define test_drv (*(route_$driver_info_t *)(va_arena + TEST_DRV_VA))

static uint32_t dest_node_arg;

static int16_t out_port;
static uint16_t out_len;
static uint16_t out_retry;
static uint16_t out_timeout;
static status_$t out_status;

static void reset_state(void)
{
    int i;

    memset(hdr_page, 0xCC, sizeof(hdr_page));
    memset(&info, 0, sizeof(info));
    memset(&test_port, 0, sizeof(test_port));
    memset(va_arena, 0, sizeof(va_arena));
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;
    memset(ROUTE_$WIRED_DATA.portp, 0, sizeof(ROUTE_$WIRED_DATA.portp));
    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));

    for (i = 0; i < (int)sizeof(template_buf); i++) {
        template_buf[i] = (uint8_t)(0x40 + i);
    }

    NODE_$ME = TEST_NODE_ME;
    NETWORK_$LOOPBACK_FLAG = 0;
    dest_node_arg = TEST_DEST_NODE;

    info.flags = 0x1234;
    info.routing_type = PKT_ROUTING_INET;
    info.addr_type = 0;
    info.protocol = 0x8031;
    info.field_0a = 0x5678;
    info.field_0c = 0x9ABC;
    for (i = 0; i < 16; i++) {
        info.addr[i] = (uint8_t)(0xA0 + i);
    }

    test_drv.max_data_len = 0x600;
    test_port.active = 2;               /* neither 0 nor 1: routable */
    test_port.port_type = 7;
    test_port.driver_info = TEST_DRV_VA;
    ROUTE_$WIRED_DATA.portp[TEST_PORT] = &test_port;
    ROUTE_$PORT_ARRAY[TEST_PORT].network = 0xDEADBEEFu;

    rip_result = 0;
    rip_status = status_$ok;
    rip_port = TEST_PORT;
    rip_nexthop_node = 0xFFF00000u | 0x00099999u;
    rip_calls = 0;

    copy_calls = 0;
    copy_src = NULL;
    copy_dst = NULL;
    copy_len = 0;

    out_port = -99;
    out_len = 0;
    out_retry = 0;
    out_timeout = 0;
    out_status = 0x5A5A5A5Au;
}

static void call_bld(uint16_t template_len, uint16_t data_len,
                     int32_t src_node_or)
{
    PKT_$BLD_INTERNET_HDR(TEST_KEY, dest_node_arg, 0x0021, src_node_or,
                          TEST_SRC_NODE, 0x0042, &info, 0x00A5,
                          template_buf, template_len, data_len,
                          &out_port, hdr, &out_len,
                          &out_retry, &out_timeout, &out_status);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * The 0x1E-byte fixed part is written for every routing type, including one
 * the routine does not understand (0x00E1206A - 0x00E1208A and the common
 * tail at 0x00E1228A).
 */
TEST(common_fixed_header)
{
    reset_state();
    info.routing_type = 9;              /* neither 1 nor 2 */
    hdr->hdr_size = 0;                  /* the tail reads it back as-is */
    call_bld(0, 0, -1);

    ASSERT_EQ(0x34, hdr->info_flags);           /* low byte of 0x1234 */
    ASSERT_EQ(0, hdr->zero_05[0]);
    ASSERT_EQ(0, hdr->zero_05[1]);
    ASSERT_EQ(0, hdr->zero_05[2]);
    ASSERT_EQ(TEST_NODE_ME, hdr->src_node);
    ASSERT_EQ(TEST_NODE_ME & 0xFFFF, hdr->src_node_lo);
    ASSERT_EQ(0x0042, hdr->src_sock);
    ASSERT_EQ(9, hdr->routing_type);
    ASSERT_EQ(0, hdr->zero_0d);
    ASSERT_EQ(0, hdr->zero_0f);
    ASSERT_EQ(0x34, hdr->flags_0e);
    ASSERT_EQ(0, hdr->template_len);
    ASSERT_EQ(0, hdr->data_len);
    ASSERT_EQ(0x00A5, hdr->request_id);
    ASSERT_EQ(0x1E, hdr->total_len);            /* 0 + 0 + 0x1E */
    ASSERT_EQ(0x1E, out_len);
    ASSERT_EQ(0, rip_calls);                    /* the RIP lookup is skipped */
    ASSERT_EQ(status_$ok, out_status);
    ASSERT_EQ(5, out_retry);
    ASSERT_EQ(4, out_timeout);
}

/* Routing type 1: one route word plus the socket, hdr_size 4 (0x00E12262). */
TEST(local_routing_header)
{
    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    call_bld(0, 0, -1);

    ASSERT_EQ(TEST_DEST_NODE, hdr->dest_node);
    ASSERT_EQ(PKT_HDR_SIZE_LOCAL, hdr->hdr_size);
    ASSERT_EQ(1, hdr->route_count);
    ASSERT_EQ(TEST_DEST_NODE & 0xFFFF, hdr->u.local.dest_node_lo);
    ASSERT_EQ(0x0021, hdr->u.local.dest_sock);
    ASSERT_EQ(0, out_port);
    ASSERT_EQ(0x22, hdr->total_len);            /* 4 + 0 + 0x1E */
    ASSERT_EQ(0, rip_calls);
}

/*
 * The loopback flag replaces the destination with this node
 * (0x00E1204C "tst.b ... / bpl" - a Domain boolean, so only < 0 counts).
 */
TEST(loopback_flag_redirects_to_this_node)
{
    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    NETWORK_$LOOPBACK_FLAG = (int8_t)0xFF;
    call_bld(0, 0, -1);
    ASSERT_EQ(TEST_NODE_ME, hdr->dest_node);
    ASSERT_EQ(TEST_NODE_ME & 0xFFFF, hdr->u.local.dest_node_lo);

    /* 0x7F is positive, so it is NOT a Domain true */
    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    NETWORK_$LOOPBACK_FLAG = 0x7F;
    call_bld(0, 0, -1);
    ASSERT_EQ(TEST_DEST_NODE, hdr->dest_node);
}

/* Routing type 2, the whole 0x28-byte internet body (0x00E1217C onwards). */
TEST(internet_routing_header)
{
    reset_state();
    call_bld(0x10, 0x40, -1);

    /* the next hop masked to 20 bits, 0x00E120CA */
    ASSERT_EQ(0x00099999u, hdr->dest_node);
    ASSERT_EQ(PKT_HDR_SIZE_INET, hdr->hdr_size);
    ASSERT_EQ(4, hdr->route_count);
    ASSERT_EQ(0x0042, hdr->u.inet.src_sock);
    ASSERT_EQ(TEST_NODE_ME, hdr->u.inet.src_node);
    ASSERT_EQ(TEST_DEST_NODE & 0xFFFF, hdr->u.inet.dest_node_lo);
    ASSERT_EQ(0x0021, hdr->u.inet.dest_sock);
    ASSERT_EQ(0x9ABC, hdr->u.inet.info_0c);
    ASSERT_EQ(0x10 + 0x1E, hdr->u.inet.tpl_len_x);
    ASSERT_EQ(0x78, hdr->u.inet.info_0b);       /* low byte of field_0a */
    ASSERT_EQ(0x31, hdr->u.inet.protocol);      /* low byte of protocol */

    ASSERT_EQ(TEST_KEY, hdr->u.inet.dest.net);
    ASSERT_EQ(0, hdr->u.inet.dest.zero);
    ASSERT_EQ(TEST_DEST_NODE, hdr->u.inet.dest.node);
    ASSERT_EQ(0x0021, hdr->u.inet.dest.sock);

    /* src_node_or == -1 takes ROUTE_$PORT_ARRAY[port].network, 0x00E121E6 */
    ASSERT_EQ(0xDEADBEEFu, hdr->u.inet.src.net);
    ASSERT_EQ(0, hdr->u.inet.src.zero);
    ASSERT_EQ(TEST_SRC_NODE, hdr->u.inet.src.node);
    ASSERT_EQ(0x0042, hdr->u.inet.src.sock);

    ASSERT_EQ(0x28 + 0x10 + 0x1E, hdr->total_len);
    ASSERT_EQ(TEST_PORT, out_port);
    ASSERT_EQ(status_$ok, out_status);
}

/* An explicit source override goes in instead (0x00E121FC). */
TEST(explicit_source_network)
{
    reset_state();
    call_bld(0, 0, 0x0BADF00D);
    ASSERT_EQ(0x0BADF00Du, hdr->u.inet.src.net);
}

/*
 * The RIP destination is built on an uninitialised slot: only the low 20
 * bits of the node are deposited ("andi.l #-0x100000" at 0x00E1209E).
 */
TEST(rip_lookup_arguments)
{
    reset_state();
    call_bld(0, 0, -1);

    ASSERT_EQ(1, rip_calls);
    ASSERT_EQ(TEST_KEY, rip_seen_dest.network);
    ASSERT_EQ(TEST_DEST_NODE, rip_seen_dest.host_lo & 0x000FFFFFu);
    ASSERT_EQ(false, rip_seen_flags);
}

/* addr_type 2 adds the 6-byte long request id (0x00E1221E - 0x00E1223C). */
TEST(long_request_id_extension)
{
    reset_state();
    info.addr_type = 2;
    info.protocol = 0x0033;
    call_bld(0, 0, -1);

    ASSERT_EQ(0x000000A5u, hdr->u.inet.long_request_id);
    ASSERT_EQ(0x0033, hdr->u.inet.subtype);
    ASSERT_EQ(4, hdr->u.inet.protocol);         /* overwritten, 0x00E12236 */
    ASSERT_EQ(0x28 + 6, hdr->hdr_size);
}

/* Subtype 0x29 adds the 16-byte address as well (0x00E12240 - 0x00E1225C). */
TEST(long_address_extension)
{
    int i;

    reset_state();
    info.addr_type = 2;
    info.protocol = PKT_SUBTYPE_LONG_ADDR;
    call_bld(0, 0, -1);

    ASSERT_EQ(0x28 + 6 + 0x10, hdr->hdr_size);
    for (i = 0; i < 16; i++) {
        ASSERT_EQ(0xA0 + i, hdr->u.inet.addr[i]);
    }
    ASSERT_EQ(0x3E + 0x1E, hdr->total_len);
}

/* Destination is this node: only the 0x1000 data cap applies (0x00E120E2). */
TEST(size_check_to_this_node)
{
    reset_state();
    dest_node_arg = TEST_NODE_ME;
    call_bld(0, 0x1000, -1);
    ASSERT_EQ(status_$ok, out_status);

    /* the driver record's much smaller cap is NOT consulted on this path */
    reset_state();
    dest_node_arg = TEST_NODE_ME;
    call_bld(0x100, 0x1000, -1);
    ASSERT_EQ(status_$ok, out_status);

    reset_state();
    dest_node_arg = TEST_NODE_ME;
    call_bld(0, 0x1001, -1);
    ASSERT_EQ(status_$network_data_length_too_large, out_status);
    /* the header is still built on the error path (0x00E1217C) */
    ASSERT_EQ(PKT_HDR_SIZE_INET, hdr->hdr_size);
}

/* Direct port: both caps come from the driver record (0x00E1211E/0x00E12136). */
TEST(size_check_direct_port)
{
    reset_state();
    call_bld(0, test_drv.max_data_len, -1);
    ASSERT_EQ(status_$ok, out_status);

    reset_state();
    call_bld(0, (uint16_t)(test_drv.max_data_len + 1), -1);
    ASSERT_EQ(status_$network_data_length_too_large, out_status);

    /* template + data may run 0x100 past the cap, but no further */
    reset_state();
    call_bld(0x100, test_drv.max_data_len, -1);
    ASSERT_EQ(status_$ok, out_status);

    reset_state();
    call_bld(0x101, test_drv.max_data_len, -1);
    ASSERT_EQ(status_$network_msg_exceeds_max_size, out_status);
}

/* active 0 or 1 means the port will not carry the packet (0x00E1210C). */
TEST(port_state_denies_request)
{
    reset_state();
    test_port.active = 0;
    call_bld(0, 0, -1);
    ASSERT_EQ(status_$network_request_denied_by_local_node, out_status);

    reset_state();
    test_port.active = 1;
    call_bld(0, 0, -1);
    ASSERT_EQ(status_$network_request_denied_by_local_node, out_status);

    reset_state();
    test_port.active = 2;
    call_bld(0, 0, -1);
    ASSERT_EQ(status_$ok, out_status);
}

/* A non-zero RIP result is a gateway route: fixed 0x400 / 0x500 caps. */
TEST(size_check_gateway)
{
    reset_state();
    rip_result = 1;
    call_bld(0, 0x400, -1);
    ASSERT_EQ(status_$ok, out_status);

    reset_state();
    rip_result = 1;
    call_bld(0, 0x401, -1);
    ASSERT_EQ(status_$network_data_length_too_large, out_status);

    reset_state();
    rip_result = 1;
    call_bld(0x100, 0x400, -1);
    ASSERT_EQ(status_$ok, out_status);

    reset_state();
    rip_result = 1;
    call_bld(0x101, 0x400, -1);
    ASSERT_EQ(status_$network_msg_exceeds_max_size, out_status);
}

/* A RIP failure skips every size check but still fills the header in. */
TEST(rip_failure_skips_size_checks)
{
    reset_state();
    rip_status = 0x003C0001;
    call_bld(0, 0xFFFF, -1);
    ASSERT_EQ(0x003C0001, out_status);
    ASSERT_EQ(PKT_HDR_SIZE_INET, hdr->hdr_size);
}

/*
 * The template lands at hdr + hdr_size + 0x1E (0x00E12300
 * "pea (-0x1,A2,D3*0x1)" with D3 = hdr_size + 0x1F).
 */
TEST(template_copy_placement)
{
    reset_state();
    call_bld(0x10, 0, -1);

    ASSERT_EQ(1, copy_calls);
    ASSERT_EQ(0x10, copy_len);
    ASSERT_EQ((long long)(uintptr_t)template_buf, (long long)(uintptr_t)copy_src);
    ASSERT_EQ((long long)(uintptr_t)(hdr_page + 0x28 + 0x1E),
              (long long)(uintptr_t)copy_dst);
    ASSERT_EQ(0x40, hdr_page[0x28 + 0x1E]);
}

/*
 * "tst.w D2w / ble" at 0x00E122D6 is SIGNED, but the 0x3B8 total-length cap
 * at 0x00E122BE is UNSIGNED and runs first, so a negative template length can
 * never reach the copy: it is rejected as an oversized header.  Both halves
 * of that are checked here, and so is the plain zero-length case that the
 * "ble" really does guard.
 */
TEST(template_length_tests)
{
    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    call_bld(0x8000, 0, -1);
    ASSERT_EQ(0, copy_calls);
    /* 4 + 0x8000 + 0x1E, truncated to a word, is still stored in the header */
    ASSERT_EQ((uint16_t)(0x8000 + 4 + 0x1E), hdr->total_len);
    ASSERT_EQ(status_$network_msg_header_too_big, out_status);

    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    call_bld(0, 0, -1);
    ASSERT_EQ(0, copy_calls);
    ASSERT_EQ(status_$ok, out_status);
    ASSERT_EQ(5, out_retry);
}

/* total_len > 0x3B8 is rejected before anything is copied (0x00E122BE). */
TEST(total_length_cap)
{
    /*
     * A total length of exactly 0x3B8 clears the first cap - out_len is
     * written - but the template would then start at 0x3B9 - 1 and the
     * second cap rejects it.  The two limits overlap by one byte in the
     * original; nothing is copied.
     */
    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    call_bld(0x3B8 - 0x22, 0, -1);              /* exactly 0x3B8 */
    ASSERT_EQ(status_$network_header_data_length_exceeds_max, out_status);
    ASSERT_EQ(0x3B8, out_len);                  /* written before the 2nd cap */
    ASSERT_EQ(0, copy_calls);

    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    out_len = 0xBEEF;
    call_bld(0x3B8 - 0x21, 0, -1);              /* 0x3B9 */
    ASSERT_EQ(status_$network_msg_header_too_big, out_status);
    ASSERT_EQ(0xBEEF, out_len);                 /* len_out is not written */
    ASSERT_EQ(0, copy_calls);
    ASSERT_EQ(0, out_retry);                    /* nor the retry / timeout */
}

/*
 * The second cap is on hdr_size + 0x1F + template_len and is a >= test
 * ("cmpi.l #0x3b8,D1 / bcs" at 0x00E122EA).
 */
TEST(template_offset_cap)
{
    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    /* 4 + 0x1F + len < 0x3B8  ->  len <= 0x394 */
    call_bld(0x394, 0, -1);
    ASSERT_EQ(status_$ok, out_status);
    ASSERT_EQ(1, copy_calls);

    reset_state();
    info.routing_type = PKT_ROUTING_LOCAL;
    call_bld(0x395, 0, -1);
    ASSERT_EQ(status_$network_header_data_length_exceeds_max, out_status);
    ASSERT_EQ(0, copy_calls);
    ASSERT_EQ(0, out_retry);
}

/* The retry limit and the response timeout are constants (0x00E1230E). */
TEST(retry_and_timeout_are_constants)
{
    reset_state();
    call_bld(0, 0, -1);
    ASSERT_EQ(5, out_retry);
    ASSERT_EQ(4, out_timeout);
}

int main(void)
{
    printf("PKT_$BLD_INTERNET_HDR tests\n");

    RUN_TEST(common_fixed_header);
    RUN_TEST(local_routing_header);
    RUN_TEST(loopback_flag_redirects_to_this_node);
    RUN_TEST(internet_routing_header);
    RUN_TEST(explicit_source_network);
    RUN_TEST(rip_lookup_arguments);
    RUN_TEST(long_request_id_extension);
    RUN_TEST(long_address_extension);
    RUN_TEST(size_check_to_this_node);
    RUN_TEST(size_check_direct_port);
    RUN_TEST(port_state_denies_request);
    RUN_TEST(size_check_gateway);
    RUN_TEST(rip_failure_skips_size_checks);
    RUN_TEST(template_copy_placement);
    RUN_TEST(template_length_tests);
    RUN_TEST(total_length_cap);
    RUN_TEST(template_offset_cap);
    RUN_TEST(retry_and_timeout_are_constants);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
