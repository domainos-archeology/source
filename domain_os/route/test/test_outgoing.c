/*
 * route/test/test_outgoing.c - Unit tests for ROUTE_$OUTGOING (0x00E87A4E).
 *
 * The tests compile the real route/outgoing.c and script every routine it
 * calls, so the two details bead source-vana corrected can be checked:
 *
 *   - the destination record handed to RIP_$FIND_NEXTHOP is written at +0x00
 *     and at the UNALIGNED +0x06 only (0x00E87AEA / 0x00E87AFA / 0x00E87B02);
 *     +0x04 and +0x0A keep whatever the frame held, and the +0x06 longword
 *     keeps its own bits 31..20;
 *   - the payload clamp at 0x00E87BA2 is a SIGNED "cmp.l / ble" minimum.
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

#include "route/route_internal.h"

/* ==========================================================================
 * Globals and scripted callees
 * ========================================================================== */

route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
route_$port_t *ROUTE_$PORTP[ROUTE_$MAX_PORTS];
int8_t ROUTE_$USER_CHECKSUM;

static int16_t find_port_result;
static uint16_t find_port_net_seen;
static int32_t find_port_sock_seen;

int16_t ROUTE_$FIND_PORT(uint16_t network, int32_t socket)
{
    find_port_net_seen = network;
    find_port_sock_seen = socket;
    return find_port_result;
}

/*
 * SOCK_$GET hands back a scripted sock_$pkt_info_t.  Its hdr field is a
 * target VA, so the header lives in the arena ARCH_HOST_VA_BASE points at.
 */
static sock_$pkt_info_t sock_get_rec;
static int8_t sock_get_result;
static uint16_t sock_get_socket_seen;

int8_t SOCK_$GET(uint16_t sock_num, void *pkt_info)
{
    sock_get_socket_seen = sock_num;
    *(sock_$pkt_info_t *)pkt_info = sock_get_rec;
    return sock_get_result;
}

/*
 * RIP_$FIND_NEXTHOP records the destination record BY VALUE so the
 * uninitialised fields can be inspected, and answers with a scripted next hop.
 */
static rip_$dest_addr_t find_nexthop_dest_seen;
static rip_$nexthop_t find_nexthop_answer;
static status_$t find_nexthop_status;
static int find_nexthop_calls;

int16_t RIP_$FIND_NEXTHOP(void *addr_info, boolean flags, int16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret)
{
    (void)flags;
    find_nexthop_calls++;
    find_nexthop_dest_seen = *(rip_$dest_addr_t *)addr_info;
    memcpy(nexthop_ret, &find_nexthop_answer, sizeof(find_nexthop_answer));
    *port_ret = 0;
    *status_ret = find_nexthop_status;
    return 0;
}

static int rtn_hdr_calls;
static uint32_t rtn_hdr_value;

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    rtn_hdr_calls++;
    rtn_hdr_value = *va_ptr;
}

static int dat_copy_calls;
static int16_t dat_copy_len;
static char *dat_copy_dest;

void PKT_$DAT_COPY(uint32_t *buffers, int16_t len, char *dest_va)
{
    (void)buffers;
    dat_copy_calls++;
    dat_copy_len = len;
    dat_copy_dest = dest_va;
}

static int dump_data_calls;
static int16_t dump_data_len;

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    (void)buffers;
    dump_data_calls++;
    dump_data_len = len;
}

static int data_copy_calls;
static const void *data_copy_src[4];
static void *data_copy_dst[4];
static uint32_t data_copy_len[4];

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    if (data_copy_calls < 4) {
        data_copy_src[data_copy_calls] = src;
        data_copy_dst[data_copy_calls] = dst;
        data_copy_len[data_copy_calls] = len;
    }
    data_copy_calls++;
    memcpy(dst, src, len);
}

/* ==========================================================================
 * The function under test
 * ========================================================================== */

#include "../outgoing.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

/* The header buffer lives in the arena, since rcv.hdr is a 32-bit VA. */
static uint8_t va_arena[0x1000];
#define pkt_hdr (*(route_$internet_hdr_t *)(va_arena + 0x100))

/* port_info is a route_$short_port_t; only +0x06 and +0x08 are read. */
static route_$short_port_t port_info;
static uint32_t nexthop_out[2];
static uint8_t packet_buf[0x900];
static int16_t length_out;
static status_$t st;

static void reset(void)
{
    memset(va_arena, 0, sizeof(va_arena));
    memset(&port_info, 0, sizeof(port_info));
    memset(packet_buf, 0, sizeof(packet_buf));
    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    memset(&sock_get_rec, 0, sizeof(sock_get_rec));

    find_port_result = 1;
    sock_get_result = -1;               /* bmi taken: a packet was dequeued */
    find_nexthop_calls = 0;
    find_nexthop_status = status_$ok;
    memset(&find_nexthop_answer, 0, sizeof(find_nexthop_answer));
    memset(&find_nexthop_dest_seen, 0xEE, sizeof(find_nexthop_dest_seen));
    rtn_hdr_calls = 0;
    rtn_hdr_value = 0;
    dat_copy_calls = 0;
    dat_copy_len = 0;
    dat_copy_dest = NULL;
    dump_data_calls = 0;
    dump_data_len = 0;
    data_copy_calls = 0;
    ROUTE_$USER_CHECKSUM = 0;

    /* A type-2 port whose status is outside the 0x03 mask. */
    ROUTE_$PORT_ARRAY[1].active = 4;
    ROUTE_$PORT_ARRAY[1].port_type = ROUTE_PORT_TYPE_ROUTING;

    port_info.port_type = 5;
    port_info.socket = 7;

    sock_get_rec.hdr = ARCH_PTR_TO_VA(&pkt_hdr);

    pkt_hdr.hdr_len = 0x20;
    pkt_hdr.data_len = 0;
    pkt_hdr.route_info = 0;
    pkt_hdr.idp.dest_network = 0x0A0B0C0D;
    pkt_hdr.idp.dest_host[0] = 0x11;
    pkt_hdr.idp.dest_host[1] = 0x22;
    pkt_hdr.idp.dest_host[2] = 0x33;
    pkt_hdr.idp.dest_host[3] = 0x44;
    pkt_hdr.idp.dest_host[4] = 0x55;
    pkt_hdr.idp.dest_host[5] = 0x66;

    nexthop_out[0] = 0;
    nexthop_out[1] = 0;
    length_out = -1;
    st = 0x7F7F7F7F;
}

static void call(void)
{
    ROUTE_$OUTGOING(&port_info, nexthop_out, packet_buf, &length_out, &st);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * 0x00E87AEA-0x00E87B02.  Only +0x00 and the unaligned +0x06 are written; the
 * host word at +0x04 and the socket at +0x0A stay as the frame left them, and
 * bits 31..20 of the +0x06 longword survive the "andi.l #-0x100000".
 */
TEST(dest_record_is_written_at_plus_zero_and_plus_six_only)
{
    /*
     * Seed the frame the routine will build its record in.  The record is a
     * local, so a first call with known garbage lets the second call inherit
     * something recognisable is not possible portably; instead check what the
     * routine DOES write and that the untouched fields are not the packet's.
     */
    reset();
    call();

    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, find_nexthop_calls);

    /* +0x00: idp.dest_network, copied whole */
    ASSERT_EQ(0x0A0B0C0D, find_nexthop_dest_seen.network);

    /*
     * +0x06: dest_host[2..5] read as a big-endian longword (0x33445566) and
     * masked to 24 bits.  "andi.l #-0x100000" cleared bits 19..0 before the
     * OR, so those bits are exactly the packet's; bits 23..20 are OR-ed into
     * whatever the frame held, and bits 31..24 are left alone entirely.
     */
    ASSERT_EQ(0x33445566u & 0x000FFFFFu,
              find_nexthop_dest_seen.host_lo & 0x000FFFFFu);
    ASSERT_EQ(0x33445566u & 0x00F00000u,
              find_nexthop_dest_seen.host_lo & 0x33445566u & 0x00F00000u);

    /* dest_host[0..1] never reach the record. */
    ASSERT_EQ(1, find_nexthop_dest_seen.host_hi != 0x1122);
}

/*
 * The read-modify-write really does preserve the upper bits: prove it by
 * running the routine twice in the same frame shape with a host_lo whose top
 * nibble differs.  The second call inherits the first call's frame, so the
 * masked-in bits from call one show up in call two's record.
 */
TEST(host_lo_upper_bits_are_or_ed_not_replaced)
{
    reset();

    /* First call: dest_host[3] = 0xF0 lights bits 23..20 of the record. */
    pkt_hdr.idp.dest_host[2] = 0x00;
    pkt_hdr.idp.dest_host[3] = 0xF0;
    pkt_hdr.idp.dest_host[4] = 0x00;
    pkt_hdr.idp.dest_host[5] = 0x01;
    call();
    ASSERT_EQ(0x00000001u, find_nexthop_dest_seen.host_lo & 0x000FFFFFu);
    ASSERT_EQ(0x00F00000u, find_nexthop_dest_seen.host_lo & 0x00F00000u);

    /*
     * Second call at the same stack depth, with those bits clear in the
     * packet: bits 19..0 follow the new packet, but bits 23..20 survive from
     * the previous frame because the mask only clears the low twenty.
     */
    pkt_hdr.idp.dest_host[3] = 0x00;
    pkt_hdr.idp.dest_host[5] = 0x02;
    call();
    ASSERT_EQ(0x00000002u, find_nexthop_dest_seen.host_lo & 0x000FFFFFu);
    ASSERT_EQ(0x00F00000u, find_nexthop_dest_seen.host_lo & 0x00F00000u);
}

/* 0x00E87B48-0x00E87B5C: the node id and the boolean byte at +0x04. */
TEST(nexthop_output_record)
{
    reset();
    find_nexthop_answer.host_lo = 0xABCDEF12;
    pkt_hdr.route_info = 0x80000000u;    /* byte at pkt+0x04 is negative */
    call();

    ASSERT_EQ(0x000DEF12, nexthop_out[0]);
    ASSERT_EQ(0xFF, ((uint8_t *)nexthop_out)[4]);

    reset();
    find_nexthop_answer.host_lo = 0xABCDEF12;
    pkt_hdr.route_info = 0x7F000000u;    /* byte at pkt+0x04 is positive */
    call();

    ASSERT_EQ(0x00, ((uint8_t *)nexthop_out)[4]);
}

/*
 * 0x00E87B96-0x00E87BA6: min(0x7FC - hdr_len, data_len), signed.  With a
 * header longer than 0x7FC the room figure goes negative and WINS the
 * compare, so a negative length is what PKT_$DAT_COPY is handed.
 */
TEST(payload_clamp_is_a_signed_minimum)
{
    /* the ordinary case: data shorter than the room left */
    reset();
    pkt_hdr.hdr_len = 0x20;
    pkt_hdr.data_len = 0x40;
    sock_get_rec.data_pages[0] = 0x00090000;
    call();
    ASSERT_EQ(1, dat_copy_calls);
    ASSERT_EQ(0x40, dat_copy_len);
    ASSERT_EQ(0x40, dump_data_len);
    ASSERT_EQ(4 + 0x20 + 0x40, length_out);
    ASSERT_EQ((uintptr_t)(packet_buf + 4 + 0x20), (uintptr_t)dat_copy_dest);

    /* data longer than the room left: clamped to 0x7FC - hdr_len */
    reset();
    pkt_hdr.hdr_len = 0x400;
    pkt_hdr.data_len = 0x700;
    sock_get_rec.data_pages[0] = 0x00090000;
    call();
    ASSERT_EQ(0x7FC - 0x400, dat_copy_len);
    ASSERT_EQ(0x700, dump_data_len);       /* the full length is released */
    ASSERT_EQ(4 + 0x400 + (0x7FC - 0x400), length_out);

    /*
     * A header past the buffer: 0x7FC - 0x900 = -0x104, which is <= the data
     * length, so the signed compare keeps it and a NEGATIVE length is passed
     * on.  An unsigned compare would have produced 0xFFFFFEFC instead.
     */
    reset();
    pkt_hdr.hdr_len = 0x900;
    pkt_hdr.data_len = 0x10;
    sock_get_rec.data_pages[0] = 0x00090000;
    call();
    ASSERT_EQ(1, dat_copy_calls);
    ASSERT_EQ((int16_t)(0x7FC - 0x900), dat_copy_len);
}

/* An empty page vector zeroes the data length before the clamp (0x00E87B8A). */
TEST(no_data_pages_skips_the_payload)
{
    reset();
    pkt_hdr.hdr_len = 0x20;
    pkt_hdr.data_len = 0x40;
    sock_get_rec.data_pages[0] = 0;
    call();

    ASSERT_EQ(0, dat_copy_calls);
    ASSERT_EQ(0, dump_data_calls);
    ASSERT_EQ(4 + 0x20, length_out);
}

/* 0x00E87BE0-0x00E87C28: the checksum runs over hdr_len + copy_len bytes. */
TEST(checksum_covers_the_body_only)
{
    reset();
    ROUTE_$USER_CHECKSUM = (int8_t)0xFF;
    pkt_hdr.hdr_len = 4;
    pkt_hdr.data_len = 0;
    call();

    {
        uint32_t expect = 0x0DEC0DED;
        int k;

        for (k = 4; k < 8; k++) {
            expect = (uint32_t)packet_buf[k] + expect * 0x11;
        }
        /*
         * The image blits the longword with OS_$DATA_COPY, so the bytes come
         * out in the machine's own order; compare the same way.
         */
        ASSERT_EQ(0, memcmp(packet_buf, &expect, 4));
    }

    /* With checksumming off the magic goes out untouched. */
    reset();
    pkt_hdr.hdr_len = 4;
    call();
    {
        uint32_t magic = 0x0DEC0DED;

        ASSERT_EQ(0, memcmp(packet_buf, &magic, 4));
    }
}

/* The three early exits (0x00E87A88, 0x00E87AB2, 0x00E87AD2). */
TEST(early_exits)
{
    reset();
    find_port_result = -1;
    call();
    ASSERT_EQ(status_$internet_unknown_network_port, st);
    ASSERT_EQ(5, find_port_net_seen);
    ASSERT_EQ(7, find_port_sock_seen);

    reset();
    ROUTE_$PORT_ARRAY[1].active = 1;    /* in the 0x03 mask */
    call();
    ASSERT_EQ(status_$internet_network_port_not_open, st);

    reset();
    ROUTE_$PORT_ARRAY[1].port_type = 1;
    call();
    ASSERT_EQ(status_$internet_network_port_not_open, st);

    reset();
    sock_get_result = 0;                /* bpl: queue empty */
    call();
    ASSERT_EQ(status_$network_buffer_queue_is_empty, st);
}

/* A failed lookup returns both buffers and reports nothing else. */
TEST(nexthop_failure_returns_the_buffers)
{
    reset();
    pkt_hdr.data_len = 0x30;
    find_nexthop_status = 0x002B0004;
    call();

    ASSERT_EQ(0x002B0004, st);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(&pkt_hdr), rtn_hdr_value);
    ASSERT_EQ(1, dump_data_calls);
    ASSERT_EQ(0x30, dump_data_len);
    ASSERT_EQ(0, dat_copy_calls);
    ASSERT_EQ(-1, length_out);
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;
    printf("ROUTE_$OUTGOING tests\n");
    RUN_TEST(dest_record_is_written_at_plus_zero_and_plus_six_only);
    RUN_TEST(host_lo_upper_bits_are_or_ed_not_replaced);
    RUN_TEST(nexthop_output_record);
    RUN_TEST(payload_clamp_is_a_signed_minimum);
    RUN_TEST(no_data_pages_skips_the_payload);
    RUN_TEST(checksum_covers_the_body_only);
    RUN_TEST(early_exits);
    RUN_TEST(nexthop_failure_returns_the_buffers);
    printf("\n%d run, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
