/*
 * mac/test/test_demux.c
 *
 * MAC_$DEMUX (0x00E0BC4E) builds a 0x40-byte sock_$pkt_info_t on its own
 * stack and hands THAT to SOCK_$PUT - not the driver record it was called
 * with.  These tests pin every field the image stores and the two flag bits it
 * sets from booleans.
 */

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Module data and stubs
 * ============================================================================ */

#include "mac/mac_internal.h"

mac_os_$channel_t MAC_OS_$CHANNEL_TABLE[MAC_OS_CHANNEL_TABLE_SLOTS];
route_$port_t    *ROUTE_$PORTP[MAC_OS_MAX_PORTS];

/* What SOCK_$PUT was handed on the last call */
static uint16_t          put_sock;
static sock_$pkt_info_t  put_desc;
static int8_t            put_flags;
static uint16_t          put_arg4;
static uint16_t          put_arg5;
static int               put_calls;
static int8_t            put_result;

int8_t SOCK_$PUT(uint16_t sock_num, sock_$pkt_info_t *pkt_info, int8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2)
{
    put_calls++;
    put_sock  = sock_num;
    put_desc  = *pkt_info;
    put_flags = flags;
    put_arg4  = ec_param1;
    put_arg5  = ec_param2;
    return put_result;
}

#include "../demux.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static mac_os_$rcv_pkt_t pkt;
static route_$port_t     port0;
static int16_t           port_num;
static int8_t            demux_flag;
static status_$t         status;

static void reset_all(void)
{
    memset(MAC_OS_$CHANNEL_TABLE, 0, sizeof(MAC_OS_$CHANNEL_TABLE));
    memset(&pkt, 0, sizeof(pkt));
    memset(&port0, 0, sizeof(port0));
    memset(&put_desc, 0, sizeof(put_desc));
    ROUTE_$PORTP[0] = &port0;

    /*
     * pkt.channel is a 32-bit target VA, so point the host VA arena at the
     * channel table and store an offset into it (arch/host/arch.h).
     */
    ARCH_HOST_VA_BASE = (uintptr_t)&MAC_OS_$CHANNEL_TABLE[0];
    port_num   = 0;
    demux_flag = 0;
    status     = 0x5A5A5A5A;
    put_calls  = 0;
    put_result = -1;                 /* SOCK_$PUT succeeded */

    MAC_OS_$CHANNEL_TABLE[3].socket = 7;
    pkt.channel = ARCH_PTR_TO_VA(&MAC_OS_$CHANNEL_TABLE[3]);
}

/* ============================================================================
 * Layout
 * ============================================================================ */

TEST(descriptor_is_a_0x40_byte_sock_pkt_info) {
    ASSERT_EQ(0x40u, sizeof(sock_$pkt_info_t));
    ASSERT_EQ(0x00u, offsetof(sock_$pkt_info_t, hdr));
    ASSERT_EQ(0x04u, offsetof(sock_$pkt_info_t, src_addr));
    ASSERT_EQ(0x08u, offsetof(sock_$pkt_info_t, src_port));
    ASSERT_EQ(0x0Cu, offsetof(sock_$pkt_info_t, dst_addr));
    ASSERT_EQ(0x10u, offsetof(sock_$pkt_info_t, flags));
    ASSERT_EQ(0x12u, offsetof(sock_$pkt_info_t, n_hops));
    ASSERT_EQ(0x14u, offsetof(sock_$pkt_info_t, hops));
    ASSERT_EQ(0x2Au, offsetof(sock_$pkt_info_t, data_len));
    ASSERT_EQ(0x2Cu, offsetof(sock_$pkt_info_t, hdr_len));
    ASSERT_EQ(0x30u, offsetof(sock_$pkt_info_t, data_pages));
}

TEST(driver_record_runs_to_0x4c) {
    /* MAC_$DEMUX copies four longwords from pkt + 0x3C (0x00E0BCC2) */
    ASSERT_EQ(0x4Cu, sizeof(mac_os_$rcv_pkt_t));
    ASSERT_EQ(0x3Cu, offsetof(mac_os_$rcv_pkt_t, data_pa));
    ASSERT_EQ(4u, sizeof(pkt.data_pa) / sizeof(pkt.data_pa[0]));
}

/* ============================================================================
 * Descriptor contents
 * ============================================================================ */

TEST(fields_come_from_the_driver_record) {
    reset_all();
    pkt.body       = 0x00110022u;   /* +0x20 -> desc + 0x00 */
    pkt.time_high  = 0x00330044u;   /* +0x2A -> desc + 0x04 */
    pkt.time_low   = 0x0055;        /* +0x2E -> desc + 0x08 */
    pkt.frame_type = 0x00660077u;   /* +0x30 -> desc + 0x0C */
    pkt.body_len   = 0x11228899;    /* +0x1E -> desc + 0x2C, LOW word */
    pkt.data_len   = 0x3344AABBu;   /* +0x3A -> desc + 0x2A, LOW word */
    pkt.data_pa[0] = 0xA0A0A0A0u;
    pkt.data_pa[1] = 0xB0B0B0B0u;
    pkt.data_pa[2] = 0xC0C0C0C0u;
    pkt.data_pa[3] = 0xD0D0D0D0u;

    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);

    ASSERT_EQ(1, put_calls);
    ASSERT_EQ(0x00110022u, put_desc.hdr);
    ASSERT_EQ(0x00330044u, put_desc.src_addr);
    ASSERT_EQ(0x0055u,     put_desc.src_port);
    ASSERT_EQ(0x00660077u, put_desc.dst_addr);
    ASSERT_EQ(0x8899u,     put_desc.hdr_len);
    ASSERT_EQ(0xAABBu,     put_desc.data_len);
    ASSERT_EQ(0xA0A0A0A0u, put_desc.data_pages[0]);
    ASSERT_EQ(0xB0B0B0B0u, put_desc.data_pages[1]);
    ASSERT_EQ(0xC0C0C0C0u, put_desc.data_pages[2]);
    ASSERT_EQ(0xD0D0D0D0u, put_desc.data_pages[3]);
}

TEST(link_address_becomes_the_hop_list) {
    reset_all();
    pkt.link_addr.n_words = 3;
    pkt.link_addr.addr[0] = 0x1111;
    pkt.link_addr.addr[1] = 0x2222;
    pkt.link_addr.addr[2] = 0x3333;

    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);

    ASSERT_EQ(3u,      put_desc.n_hops);
    ASSERT_EQ(0x1111u, put_desc.hops[0]);
    ASSERT_EQ(0x2222u, put_desc.hops[1]);
    ASSERT_EQ(0x3333u, put_desc.hops[2]);
}

TEST(flags_word_starts_at_two) {
    reset_all();
    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);
    ASSERT_EQ(SOCK_PKT_FLAG_XNS, put_desc.flags);
}

TEST(local_flag_is_bit_zero) {
    reset_all();
    pkt.is_local = (int8_t)0xFF;            /* Domain true */
    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);
    ASSERT_EQ(SOCK_PKT_FLAG_XNS | SOCK_PKT_FLAG_LOCAL, put_desc.flags);
}

TEST(demux_boolean_is_bit_two) {
    reset_all();
    demux_flag = (int8_t)0xFF;
    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);
    ASSERT_EQ(SOCK_PKT_FLAG_XNS | SOCK_PKT_FLAG_DEMUX_BOOL, put_desc.flags);
}

TEST(both_extra_flags_together) {
    reset_all();
    pkt.is_local = (int8_t)0xFF;
    demux_flag   = (int8_t)0xFF;
    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);
    ASSERT_EQ(0x0007u, put_desc.flags);
}

/* ============================================================================
 * SOCK_$PUT arguments and status
 * ============================================================================ */

TEST(sock_put_arguments) {
    reset_all();
    port0.port_type = 0x1234;       /* route_$port_t + 0x2E */
    port0.socket    = 0x5678;       /* route_$port_t + 0x30 */

    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);

    ASSERT_EQ(7u,      put_sock);   /* the channel's socket, not the port's */
    ASSERT_EQ(0,       put_flags);
    ASSERT_EQ(0x1234u, put_arg4);
    ASSERT_EQ(0x5678u, put_arg5);
    ASSERT_EQ(status_$ok, status);
}

TEST(no_socket_is_rejected_before_sock_put) {
    reset_all();
    MAC_OS_$CHANNEL_TABLE[3].socket = MAC_NO_SOCKET;
    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);
    ASSERT_EQ(0, put_calls);
    ASSERT_EQ(status_$mac_XXX_unknown, status);
}

TEST(sock_put_failure_is_reported) {
    reset_all();
    put_result = 0;                 /* non-negative means "not queued" */
    MAC_$DEMUX(&pkt, &port_num, &demux_flag, &status);
    ASSERT_EQ(1, put_calls);
    ASSERT_EQ(status_$mac_failed_to_put_packet_into_socket, status);
}

int main(void)
{
    printf("MAC_$DEMUX tests\n");
    RUN_TEST(descriptor_is_a_0x40_byte_sock_pkt_info);
    RUN_TEST(driver_record_runs_to_0x4c);
    RUN_TEST(fields_come_from_the_driver_record);
    RUN_TEST(link_address_becomes_the_hop_list);
    RUN_TEST(flags_word_starts_at_two);
    RUN_TEST(local_flag_is_bit_zero);
    RUN_TEST(demux_boolean_is_bit_two);
    RUN_TEST(both_extra_flags_together);
    RUN_TEST(sock_put_arguments);
    RUN_TEST(no_socket_is_rejected_before_sock_put);
    RUN_TEST(sock_put_failure_is_reported);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
