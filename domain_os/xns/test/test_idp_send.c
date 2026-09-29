/*
 * xns/test/test_idp_send.c - Unit tests for XNS_IDP_$OS_SEND (0x00E18256)
 * and XNS_IDP_$SEND (0x00E18A66).
 *
 * xns/idp_send.c is #included below, so the code under test is the real
 * thing; every callee is stubbed here and records what it was handed.
 *
 * The behaviours covered are the ones the re-emission for source-tvrs fixed:
 *   - the record handed to MAC_OS_$SEND is a real mac_os_$send_pkt_t: frame
 *     type at +0x30, the caller's {length, address, next} triple at +0x1C,
 *     hdr_prebuilt at +0x28, data_length and the four pages at +0x38
 *   - MAC_OS_$SEND's first argument is the port's MAC channel word inside
 *     the IDP state, not a ROUTE port record (0x00E18474)
 *   - the checksum is computed when the header does NOT already say 0xFFFF
 *     (0x00E1842E "cmpi.w #-1,(A3) / beq")
 *   - the connect/build-header flags are word bits 13 and 11, because the
 *     original tests them with byte btst on the high half (0x00E182A4)
 *   - the request record's first 24 bytes are the destination address
 *     followed by the source address (source-2ptk, 0x00E1833C)
 *   - the third argument is the length MAC_OS_$SEND reports, not a checksum
 */

#include <stdio.h>
#include <string.h>

#include "xns/xns_internal.h"

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
 * Globals the send path touches
 * ============================================================================ */

MODULE_DATA_DEFINE(xns_$idp_data_t, XNS_IDP_$DATA, 0x00E2B314);
uint16_t                PROC1_$AS_ID;

/* ============================================================================
 * Recorded call state
 * ============================================================================ */

static status_$t fim_cleanup_result;
static int       fim_cleanup_calls;
static int       fim_release_calls;

static int              find_nexthop_calls;
static rip_$dest_addr_t find_nexthop_dest;
static boolean          find_nexthop_flag;
static int16_t          find_nexthop_port;
static status_$t        find_nexthop_status;

static int          arp_calls;
static int16_t      arp_port;
static uint16_t    *arp_link_addr_p;
static uint8_t     *arp_bcast_p;
static status_$t    arp_status;

static int          add_port_calls;
static uint16_t     add_port_channel;
static int16_t      add_port_port;
static status_$t    add_port_status;

static int          chksum_calls;
static void        *chksum_arg;
static int16_t      chksum_result;

static int                 send_calls;
static int16_t            *send_channel_p;
static mac_os_$send_pkt_t  send_rec_copy;
static int16_t             send_len;
static status_$t           send_status;

/* ============================================================================
 * Stubs
 * ============================================================================ */

status_$t FIM_$CLEANUP(void *handler)
{
    (void)handler;
    fim_cleanup_calls++;
    return fim_cleanup_result;
}

void FIM_$RLS_CLEANUP(void *handler)
{
    (void)handler;
    fim_release_calls++;
}

int16_t RIP_$FIND_NEXTHOP(void *addr_info, boolean flags, int16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret)
{
    find_nexthop_calls++;
    find_nexthop_dest = *(rip_$dest_addr_t *)addr_info;
    find_nexthop_flag = flags;
    *port_ret = find_nexthop_port;
    memset(nexthop_ret, 0xAB, sizeof(rip_$nexthop_t));
    *status_ret = find_nexthop_status;
    return 0;
}

void MAC_OS_$ARP(void *addr_info, int16_t port_num, uint16_t *mac_addr,
                 uint8_t *flags, status_$t *status_ret)
{
    (void)addr_info;
    arp_calls++;
    arp_port = port_num;
    arp_link_addr_p = mac_addr;
    arp_bcast_p = flags;
    *status_ret = arp_status;
}

void xns_$add_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    add_port_calls++;
    add_port_channel = channel;
    add_port_port = port;
    *status_ret = add_port_status;
}

int16_t xns_$get_checksum(void *packet_info)
{
    chksum_calls++;
    chksum_arg = packet_info;
    return chksum_result;
}

void MAC_OS_$SEND(int16_t *channel, mac_os_$send_pkt_t *pkt_desc,
                  int16_t *bytes_sent, status_$t *status_ret)
{
    send_calls++;
    send_channel_p = channel;
    send_rec_copy = *pkt_desc;
    *bytes_sent = send_len;
    *status_ret = send_status;
}

#include "../idp_send.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

#define TEST_CHANNEL 3
#define TEST_PORT    2

/*
 * The records the send path walks hold m68k 32-bit virtual addresses, not
 * host pointers, so everything the code under test dereferences through one
 * of those fields lives in this arena and is named by its offset.
 * ARCH_HOST_VA_BASE (arch/host/arch.h) makes ARCH_VA_TO_PTR resolve those
 * offsets back to real pointers.
 */
_Alignas(16) static uint8_t arena[0x1000];

#define VA_HDR        0x0100
#define VA_BUF1       0x0200
#define VA_BUF2       0x0220
#define VA_PAYLOAD    0x0300
#define VA_USER_IOV1  0x0400

#define ARENA_AT(type, off) ((type *)(arena + (off)))

static int16_t              channel_no;
static xns_$os_send_rec_t   rec;
static xns_$idp_header_t   *hdr;
static mac_os_$buf_desc_t  *buf1;
static mac_os_$buf_desc_t  *buf2;
static uint8_t             *payload;

static void setup(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)arena;

    memset(arena, 0, sizeof(arena));
    memset(&XNS_IDP_$DATA, 0, sizeof(XNS_IDP_$DATA));
    memset(&rec, 0, sizeof(rec));
    memset(&send_rec_copy, 0, sizeof(send_rec_copy));

    hdr     = ARENA_AT(xns_$idp_header_t, VA_HDR);
    buf1    = ARENA_AT(mac_os_$buf_desc_t, VA_BUF1);
    buf2    = ARENA_AT(mac_os_$buf_desc_t, VA_BUF2);
    payload = arena + VA_PAYLOAD;

    channel_no = TEST_CHANNEL;

    fim_cleanup_result = status_$cleanup_handler_set;
    fim_cleanup_calls = 0;
    fim_release_calls = 0;

    find_nexthop_calls = 0;
    find_nexthop_port = TEST_PORT;
    find_nexthop_status = status_$ok;
    find_nexthop_flag = false;

    arp_calls = 0;
    arp_status = status_$ok;

    add_port_calls = 0;
    add_port_status = status_$ok;

    chksum_calls = 0;
    chksum_result = 0x1234;

    send_calls = 0;
    send_len = 0x40;
    send_status = status_$ok;

    PROC1_$AS_ID = 5;

    /* A plain unconnected channel that asks for a header build. */
    XNS_IDP_$DATA.channels[TEST_CHANNEL].flags =
        XNS_CHAN_FLAG_BUILD_HEADER | (5 << XNS_CHAN_FLAG_AS_ID_SHIFT);
    XNS_IDP_$DATA.channels[TEST_CHANNEL].state = (int16_t)0x8000;

    /* The request: a 30-byte header buffer plus two chained payload buffers */
    rec.hdr_desc.length  = XNS_IDP_HEADER_SIZE;
    rec.hdr_desc.address = VA_HDR;
    rec.hdr_desc.next    = VA_BUF1;

    buf1->length  = 10;
    buf1->address = VA_PAYLOAD;
    buf1->next    = VA_BUF2;

    buf2->length  = 6;
    buf2->address = VA_PAYLOAD + 16;
    buf2->next    = 0;

    rec.packet_type = 0x0004;      /* only the low byte is used */
    rec.hdr_prebuilt = false;
    rec.data_length = 0;

    rec.dest_addr.network = 0x11223344;
    rec.dest_addr.host[0] = 0xA0; rec.dest_addr.host[1] = 0xA1;
    rec.dest_addr.host[2] = 0xA2; rec.dest_addr.host[3] = 0xA3;
    rec.dest_addr.host[4] = 0xA4; rec.dest_addr.host[5] = 0xA5;
    rec.dest_addr.socket  = 0x5566;

    rec.src_addr.network = 0x99887766;
    rec.src_addr.host[0] = 0xB0; rec.src_addr.host[1] = 0xB1;
    rec.src_addr.host[2] = 0xB2; rec.src_addr.host[3] = 0xB3;
    rec.src_addr.host[4] = 0xB4; rec.src_addr.host[5] = 0xB5;
    rec.src_addr.socket  = 0x7788;
}

/* ============================================================================
 * XNS_IDP_$OS_SEND
 * ============================================================================ */

/* The FIM cleanup handler failing returns its status and does NOT release. */
static void test_cleanup_handler_fault(void)
{
    int16_t   len = 0x55;
    status_$t s = 0x55;

    fim_cleanup_result = 0x00120022;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(0x00120022, s);
    ASSERT_EQ(0, len);              /* 0x00E18268 cleared it first */
    ASSERT_EQ(0, fim_release_calls);
    ASSERT_EQ(0, send_calls);
}

/*
 * source-2ptk: with the build-header flag set and the channel unconnected,
 * record +0x00..+0x17 becomes the header's address block at +0x06.
 */
static void test_header_built_from_record_addresses(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(0x11223344, hdr->dest_network);
    ASSERT_EQ(0xA0, hdr->dest_host[0]);
    ASSERT_EQ(0xA5, hdr->dest_host[5]);
    ASSERT_EQ(0x5566, hdr->dest_socket);
    ASSERT_EQ(0x99887766, hdr->src_network);
    ASSERT_EQ(0xB0, hdr->src_host[0]);
    ASSERT_EQ(0xB5, hdr->src_host[5]);
    ASSERT_EQ(0x7788, hdr->src_socket);

    /* 0x00E1830A / 0x00E1830E */
    ASSERT_EQ(0, hdr->transport_ctl);
    ASSERT_EQ(0x04, hdr->packet_type);
}

/* 0x00E182C4-0x00E18306: hdr_desc.length + every chained length + data_length */
static void test_length_sums_the_whole_chain(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    rec.data_length = 0x30;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(XNS_IDP_HEADER_SIZE + 10 + 6 + 0x30, hdr->length);
}

/* 0x00E182D0: a negative buffer length is rejected */
static void test_negative_buffer_length_rejected(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    buf1->length = -1;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$xns_illegal_buffer_spec, s);
    ASSERT_EQ(1, fim_release_calls);
    ASSERT_EQ(0, send_calls);
}

/* 0x00E182D2-0x00E182D8: a positive length with a nil address is rejected */
static void test_nil_buffer_address_rejected(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    buf2->address = 0;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$xns_illegal_buffer_spec, s);
    ASSERT_EQ(0, send_calls);
}

/* ... but a ZERO length with a nil address is fine (the `ble' at 0x00E182D2) */
static void test_zero_length_nil_address_allowed(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    buf2->length = 0;
    buf2->address = 0;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(1, send_calls);
}

/* 0x00E1836E: the destination handed to RIP_$FIND_NEXTHOP comes from hdr+6 */
static void test_nexthop_lookup_uses_header_destination(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(1, find_nexthop_calls);
    ASSERT_EQ(0x11223344, find_nexthop_dest.network);
    ASSERT_EQ(0xA0A1, find_nexthop_dest.host_hi);
    ASSERT_EQ(0xA2A3A4A5, find_nexthop_dest.host_lo);
    ASSERT_EQ(0x5566, find_nexthop_dest.socket);
    /* 0x00E1837C `st -(SP)' - a Pascal boolean true by value */
    ASSERT_EQ((unsigned char)true, (unsigned char)find_nexthop_flag);
}

/* 0x00E1839E: port -1 is "network unreachable" */
static void test_unreachable_network(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    find_nexthop_port = -1;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$xns_network_unreachable, s);
    ASSERT_EQ(1, fim_release_calls);
    ASSERT_EQ(0, arp_calls);
    ASSERT_EQ(0, send_calls);
}

/* 0x00E18394: a lookup failure releases the handler and stops */
static void test_nexthop_failure(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    find_nexthop_status = 0x003C0001;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(0x003C0001, s);
    ASSERT_EQ(1, fim_release_calls);
    ASSERT_EQ(0, arp_calls);
}

/*
 * 0x00E183BC-0x00E183CC: ARP is given the record's link address (+0x00) and
 * its broadcast flag (+0x18), i.e. two fields of the SAME record.
 */
static void test_arp_targets_the_send_record(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(1, arp_calls);
    ASSERT_EQ(TEST_PORT, arp_port);
    /* the two pointers are 0x18 bytes apart, in the same object */
    ASSERT_EQ(0x18, (const uint8_t *)arp_bcast_p - (const uint8_t *)arp_link_addr_p);
}

/* 0x00E183DA */
static void test_arp_failure(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    arp_status = status_$mac_arp_address_not_found;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$mac_arp_address_not_found, s);
    ASSERT_EQ(0, add_port_calls);
    ASSERT_EQ(1, fim_release_calls);
}

/* 0x00E183E0-0x00E183F8 */
static void test_add_port_arguments_and_failure(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    add_port_status = status_$xns_listen_network_not_connected;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(1, add_port_calls);
    ASSERT_EQ(TEST_CHANNEL, add_port_channel);
    ASSERT_EQ(TEST_PORT, add_port_port);
    ASSERT_EQ(status_$xns_listen_network_not_connected, s);
    ASSERT_EQ(0, send_calls);
}

/* source-tvrs: the record MAC_OS_$SEND is handed is a mac_os_$send_pkt_t */
static void test_mac_send_record_contents(void)
{
    int16_t   len = 0;
    status_$t s = 0;
    int       i;

    rec.hdr_prebuilt = true;
    rec.data_length = 0x111;
    rec.data_pages[0] = 0x1000;
    rec.data_pages[1] = 0x2000;
    rec.data_pages[2] = 0x3000;
    rec.data_pages[3] = 0x4000;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(XNS_MAC_FRAME_TYPE, send_rec_copy.frame_type);
    ASSERT_EQ(XNS_IDP_HEADER_SIZE, send_rec_copy.hdr_desc.length);
    ASSERT_EQ(VA_HDR, send_rec_copy.hdr_desc.address);
    ASSERT_EQ(VA_BUF1, send_rec_copy.hdr_desc.next);
    ASSERT_EQ((unsigned char)true, (unsigned char)send_rec_copy.hdr_prebuilt);
    ASSERT_EQ(0x111, send_rec_copy.data_length);
    for (i = 0; i < 4; i++) {
        ASSERT_EQ(0x1000 * (i + 1), send_rec_copy.data_pages[i]);
    }
}

/*
 * 0x00E18474: MAC_OS_$SEND's channel argument is the port's MAC channel word
 * inside the IDP state, at state +0x40 + port*12 + 8.
 */
static void test_mac_send_channel_argument(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_PTR(&XNS_IDP_$DATA.ports[TEST_PORT].mac_socket, send_channel_p);
    ASSERT_PTR(XNS_IDP_$PORT_MAC_CHANNEL(TEST_PORT), send_channel_p);
}

/* 0x00E18460: the third argument is the length MAC_OS_$SEND reports */
static void test_length_is_reported_back(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    send_len = 0x2A;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(0x2A, len);
    ASSERT_EQ(1, XNS_IDP_$DATA.packets_sent);       /* 0x00E1848A */
}

/* 0x00E18486: a failed send counts nothing */
static void test_failed_send_counts_nothing(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    send_status = status_$mac_illegal_buffer_spec;
    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$mac_illegal_buffer_spec, s);
    ASSERT_EQ(0, XNS_IDP_$DATA.packets_sent);
    ASSERT_EQ(1, fim_release_calls);
}

/*
 * 0x00E1842E: the checksum is computed only when the header does NOT hold
 * 0xFFFF.  The build-header path always stores 0xFFFF first (0x00E182BC), so
 * a header build implies no checksum.
 */
static void test_no_checksum_when_header_built(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(0, chksum_calls);
    ASSERT_EQ(0xFFFF, hdr->checksum);
}

static void test_checksum_computed_for_prebuilt_header(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    /* no header build, and the caller left a checksum request in place */
    XNS_IDP_$DATA.channels[TEST_CHANNEL].flags &= (uint16_t)~XNS_CHAN_FLAG_BUILD_HEADER;
    XNS_IDP_$DATA.channels[TEST_CHANNEL].flags |= XNS_CHAN_FLAG_CONNECT;
    XNS_IDP_$DATA.channels[TEST_CHANNEL].connected_port = TEST_PORT;
    hdr->checksum = 0;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(1, chksum_calls);
    /* 0x00E18434: the argument is the MAC send record itself */
    ASSERT_EQ(0x1234, hdr->checksum);
    ASSERT_EQ(1, send_calls);
}

/* 0x00E18440: a checksum of -1 is refused */
static void test_bad_checksum(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    XNS_IDP_$DATA.channels[TEST_CHANNEL].flags &= (uint16_t)~XNS_CHAN_FLAG_BUILD_HEADER;
    XNS_IDP_$DATA.channels[TEST_CHANNEL].flags |= XNS_CHAN_FLAG_CONNECT;
    XNS_IDP_$DATA.channels[TEST_CHANNEL].connected_port = TEST_PORT;
    hdr->checksum = 0;
    chksum_result = -1;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$xns_bad_checksum, s);
    ASSERT_EQ(0, send_calls);
    ASSERT_EQ(1, fim_release_calls);
}

/*
 * 0x00E18318-0x00E18358: a connected channel supplies both the header
 * addresses and the link address, and never consults RIP or ARP.
 */
static void test_connected_channel(void)
{
    int16_t   len = 0;
    status_$t s = 0;
    xns_$channel_t *chan = &XNS_IDP_$DATA.channels[TEST_CHANNEL];
    int i;

    chan->flags |= XNS_CHAN_FLAG_CONNECT;
    chan->connected_port = TEST_PORT;
    chan->dest_network = 0xDEADBEEF;
    for (i = 0; i < 6; i++) {
        chan->dest_host[i] = (uint8_t)(0xC0 + i);
    }
    chan->dest_socket = 0x0451;
    chan->src_network = 0xFEEDFACE;
    for (i = 0; i < 6; i++) {
        chan->src_host[i] = (uint8_t)(0xD0 + i);
    }
    chan->src_port = 0x0BB9;
    for (i = 0; i < 0x18; i++) {
        chan->mac_info[i] = (uint8_t)(0x10 + i);
    }

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(0, find_nexthop_calls);
    ASSERT_EQ(0, arp_calls);
    ASSERT_EQ(TEST_PORT, add_port_port);

    ASSERT_EQ(0xDEADBEEF, hdr->dest_network);
    ASSERT_EQ(0xC5, hdr->dest_host[5]);
    ASSERT_EQ(0x0451, hdr->dest_socket);
    ASSERT_EQ(0xFEEDFACE, hdr->src_network);
    ASSERT_EQ(0xD5, hdr->src_host[5]);
    ASSERT_EQ(0x0BB9, hdr->src_socket);

    /* the six longwords landed in the record's first 24 bytes */
    for (i = 0; i < 0x18; i++) {
        ASSERT_EQ(0x10 + i, ((const uint8_t *)&send_rec_copy)[i]);
    }
}

/*
 * With neither flag set the header is left alone entirely, which is the RIP
 * and XNS_ERROR case (their channels build their own IDP header).
 */
static void test_no_build_no_connect(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    XNS_IDP_$DATA.channels[TEST_CHANNEL].flags &= (uint16_t)~XNS_CHAN_FLAG_BUILD_HEADER;
    hdr->checksum = 0xFFFF;
    hdr->length = 0x1234;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(0x1234, hdr->length);      /* untouched */
    ASSERT_EQ(1, find_nexthop_calls);   /* still routed */
    ASSERT_EQ(0, chksum_calls);
}

/*
 * The connect and build-header bits are word bits 13 and 11 (byte btst on
 * the high half at 0x00E182A4/0x00E182AC); the AS_ID in bits 5..10 must not
 * be able to turn either of them on.
 */
static void test_flag_bits_do_not_collide_with_as_id(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    /* every AS_ID bit set, nothing else */
    XNS_IDP_$DATA.channels[TEST_CHANNEL].flags = XNS_CHAN_FLAG_AS_ID_MASK;
    hdr->checksum = 0xFFFF;

    XNS_IDP_$OS_SEND(&channel_no, &rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(1, find_nexthop_calls);   /* not connected */
    ASSERT_EQ(0, hdr->transport_ctl);    /* header was not built */
    ASSERT_EQ(0, hdr->length);
}

/* ============================================================================
 * XNS_IDP_$SEND
 * ============================================================================ */

#define VA_USER_IOV2  0x0420

static xns_$idp_send_t  user_rec;
static xns_$idp_iov_t  *user_iov1;
static xns_$idp_iov_t  *user_iov2;
static uint16_t         user_channel;

static void setup_user(void)
{
    memset(&user_rec, 0, sizeof(user_rec));

    user_channel = TEST_CHANNEL;

    user_rec.hdr_desc.length  = XNS_IDP_HEADER_SIZE;
    user_rec.hdr_desc.address = VA_HDR;
    user_rec.hdr_desc.next    = VA_USER_IOV1;

    /* the whole chain is linked by virtual address, as in the binary */
    user_iov1 = ARENA_AT(xns_$idp_iov_t, VA_USER_IOV1);
    user_iov1->desc.length  = 10;
    user_iov1->desc.address = VA_PAYLOAD;
    user_iov1->desc.next    = VA_USER_IOV2;
    user_iov1->flags        = 0xFF;

    user_iov2 = ARENA_AT(xns_$idp_iov_t, VA_USER_IOV2);
    user_iov2->desc.length  = 6;
    user_iov2->desc.address = VA_PAYLOAD + 16;
    user_iov2->desc.next    = 0;
    user_iov2->flags        = 0xFF;

    user_rec.packet_type = 0x0004;
    user_rec.dest_addr = rec.dest_addr;
    user_rec.src_addr  = rec.src_addr;
}

/* 0x00E18A84: the bound is unsigned */
static void test_user_channel_out_of_range(void)
{
    int16_t   len = 0x33;
    status_$t s = 0;

    setup_user();
    user_channel = XNS_MAX_CHANNELS;
    XNS_IDP_$SEND(&user_channel, &user_rec, &len, &s);

    ASSERT_EQ(status_$xns_bad_channel, s);
    ASSERT_EQ(0, len);
    ASSERT_EQ(0, fim_cleanup_calls);
}

/* 0x00E18A98: a channel whose state word is non-negative is not open */
static void test_user_channel_not_open(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    setup_user();
    XNS_IDP_$DATA.channels[TEST_CHANNEL].state = 0;
    XNS_IDP_$SEND(&user_channel, &user_rec, &len, &s);

    ASSERT_EQ(status_$xns_bad_channel, s);
}

/* 0x00E18A9E-0x00E18AAE */
static void test_user_channel_wrong_owner(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    setup_user();
    PROC1_$AS_ID = 6;
    XNS_IDP_$SEND(&user_channel, &user_rec, &len, &s);

    ASSERT_EQ(status_$xns_bad_channel, s);
}

/* 0x00E18AD8-0x00E18AE6 */
static void test_user_header_too_short(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    setup_user();
    user_rec.hdr_desc.length = XNS_IDP_HEADER_SIZE - 1;
    XNS_IDP_$SEND(&user_channel, &user_rec, &len, &s);

    ASSERT_EQ(status_$xns_illegal_buffer_spec, s);
    ASSERT_EQ(1, fim_release_calls);
    ASSERT_EQ(0, send_calls);
}

static void test_user_nil_header(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    setup_user();
    user_rec.hdr_desc.address = 0;
    XNS_IDP_$SEND(&user_channel, &user_rec, &len, &s);

    ASSERT_EQ(status_$xns_illegal_buffer_spec, s);
}

/*
 * 0x00E18B00-0x00E18B42: the user's addresses, buffer descriptor and packet
 * type reach XNS_IDP_$OS_SEND, hdr_prebuilt is forced false, and every
 * buffer's flag byte is cleared.
 */
static void test_user_request_is_copied(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    setup_user();
    XNS_IDP_$SEND(&user_channel, &user_rec, &len, &s);

    ASSERT_EQ(status_$ok, s);
    ASSERT_EQ(1, send_calls);

    /* the addresses made it all the way into the IDP header */
    ASSERT_EQ(0x11223344, hdr->dest_network);
    ASSERT_EQ(0x99887766, hdr->src_network);
    ASSERT_EQ(0x04, hdr->packet_type);

    /* hdr_prebuilt was forced false (0x00E18B2C) */
    ASSERT_EQ(0, (unsigned char)send_rec_copy.hdr_prebuilt);
    /* data_length and the first page were cleared (0x00E18B16/0x00E18B1A) */
    ASSERT_EQ(0, send_rec_copy.data_length);
    ASSERT_EQ(0, send_rec_copy.data_pages[0]);

    /* 0x00E18B36: every user buffer's flag byte is cleared */
    ASSERT_EQ(0, user_iov1->flags);
    ASSERT_EQ(0, user_iov2->flags);

    ASSERT_EQ(0x40, len);
}

/* 0x00E18B5C-0x00E18B6C: both outputs come from XNS_IDP_$OS_SEND */
static void test_user_reports_inner_failure(void)
{
    int16_t   len = 0;
    status_$t s = 0;

    setup_user();
    send_status = status_$mac_illegal_buffer_spec;
    send_len = 0;
    XNS_IDP_$SEND(&user_channel, &user_rec, &len, &s);

    ASSERT_EQ(status_$mac_illegal_buffer_spec, s);
    ASSERT_EQ(0, len);
    /* one release for the inner call, one for the outer */
    ASSERT_EQ(2, fim_release_calls);
}

int main(void)
{
    printf("Running XNS_IDP_$OS_SEND / XNS_IDP_$SEND tests...\n");

    RUN_TEST(cleanup_handler_fault);
    RUN_TEST(header_built_from_record_addresses);
    RUN_TEST(length_sums_the_whole_chain);
    RUN_TEST(negative_buffer_length_rejected);
    RUN_TEST(nil_buffer_address_rejected);
    RUN_TEST(zero_length_nil_address_allowed);
    RUN_TEST(nexthop_lookup_uses_header_destination);
    RUN_TEST(unreachable_network);
    RUN_TEST(nexthop_failure);
    RUN_TEST(arp_targets_the_send_record);
    RUN_TEST(arp_failure);
    RUN_TEST(add_port_arguments_and_failure);
    RUN_TEST(mac_send_record_contents);
    RUN_TEST(mac_send_channel_argument);
    RUN_TEST(length_is_reported_back);
    RUN_TEST(failed_send_counts_nothing);
    RUN_TEST(no_checksum_when_header_built);
    RUN_TEST(checksum_computed_for_prebuilt_header);
    RUN_TEST(bad_checksum);
    RUN_TEST(connected_channel);
    RUN_TEST(no_build_no_connect);
    RUN_TEST(flag_bits_do_not_collide_with_as_id);

    RUN_TEST(user_channel_out_of_range);
    RUN_TEST(user_channel_not_open);
    RUN_TEST(user_channel_wrong_owner);
    RUN_TEST(user_header_too_short);
    RUN_TEST(user_nil_header);
    RUN_TEST(user_request_is_copied);
    RUN_TEST(user_reports_inner_failure);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
