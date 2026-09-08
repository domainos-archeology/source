/*
 * app/test/test_demux.c - APP_$DEMUX (0x00E00A90), bead source-jb4t
 *
 * Two things the bead names:
 *
 *   - the direct-return condition at 0x00E00ABC-0x00E00AD6 ends with
 *     "tst.b (A1) / bmi.w 0x00E00B66", so the packet is dropped when the
 *     caller's boolean is TRUE, not when it is false;
 *   - the record SOCK_$PUT is given at A6-0x40 is ONE sock_$pkt_info_t, so
 *     the fields the routine writes must land at +0x00, +0x04, +0x08, +0x10,
 *     +0x12, +0x2A, +0x2C and +0x30 of a single object.
 *
 * app/demux.c is #included below so the function under test is the real one.
 */

#include <stdio.h>
#include <string.h>

#include "app/app_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed;

#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    setup();                                    \
    name();                                     \
    if (current_failed) {                       \
        tests_failed++;                         \
        printf("FAIL\n");                       \
    } else {                                    \
        tests_passed++;                         \
        printf("ok\n");                         \
    }                                           \
} while (0)

#define ASSERT_EQ(expected, actual, what) do {                          \
    unsigned long _e = (unsigned long)(expected);                       \
    unsigned long _a = (unsigned long)(actual);                         \
    if (_a != _e) {                                                     \
        printf("\n    %s: got 0x%lx, expected 0x%lx", (what), _a, _e);  \
        current_failed = 1;                                             \
    }                                                                   \
} while (0)

/* ============================================================================
 * Globals
 * ============================================================================ */

uint16_t RING_$FILE_OVERFLOW;
uint16_t RING_$OVERFLOW_OVERFLOW;

/* The packet buffer the descriptor's header VA points at. */
#define TARGET_VA_BASE 0x00100000u
static uint8_t target_arena[0x1000];

static uint32_t ptr_to_va(const void *p)
{
    return (uint32_t)((const uint8_t *)p - target_arena) + TARGET_VA_BASE;
}

/* ============================================================================
 * Stubs
 * ============================================================================ */

static int              sock_put_calls;
static uint16_t         sock_put_sock[4];
static int8_t           sock_put_flags[4];
static uint16_t         sock_put_p4[4];
static uint16_t         sock_put_p5[4];
static const void      *sock_put_rec_ptr[4];
static sock_$pkt_info_t sock_put_rec[4];
static int8_t           sock_put_result[4];

int8_t SOCK_$PUT(uint16_t sock_num, sock_$pkt_info_t *pkt_info, int8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2)
{
    int8_t answer = 0;

    if (sock_put_calls < 4) {
        sock_put_sock[sock_put_calls] = sock_num;
        sock_put_flags[sock_put_calls] = flags;
        sock_put_p4[sock_put_calls] = ec_param1;
        sock_put_p5[sock_put_calls] = ec_param2;
        sock_put_rec_ptr[sock_put_calls] = pkt_info;
        sock_put_rec[sock_put_calls] = *pkt_info;
        answer = sock_put_result[sock_put_calls];
    }
    sock_put_calls++;
    return answer;
}

static int      rtn_hdr_calls;
static uint32_t rtn_hdr_va;

void NETBUF_$RTN_HDR(uint32_t *hdr)
{
    rtn_hdr_calls++;
    rtn_hdr_va = *hdr;
}

static int       dump_calls;
static uint32_t  dump_first_page;
static int16_t   dump_len;

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    dump_calls++;
    dump_first_page = buffers[0];
    dump_len = len;
}

/* The code under test, for real. */
#include "../demux.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

static xns_$pkt_desc_t pkt;
static app_pkt_hdr_t  *app_hdr;
static uint32_t        hdr_va;
static uint16_t        port_type;
static uint16_t        port_socket;
static boolean         mac_broadcast;
static status_$t       st;

#define TEST_SOCK 0x0030

static void setup(void)
{
    int i;

    memset(target_arena, 0, sizeof(target_arena));
    memset(&pkt, 0, sizeof(pkt));

    sock_put_calls = 0;
    for (i = 0; i < 4; i++) {
        sock_put_result[i] = -1;        /* queued */
    }
    rtn_hdr_calls = 0;
    dump_calls = 0;
    RING_$FILE_OVERFLOW = 0;
    RING_$OVERFLOW_OVERFLOW = 0;

    ARCH_HOST_VA_BASE = (uintptr_t)target_arena - (uintptr_t)TARGET_VA_BASE;

    /* The IDP header sits at hdr_va; the application header follows it. */
    hdr_va = TARGET_VA_BASE + 0x0100;
    app_hdr = (app_pkt_hdr_t *)ARCH_VA_TO_PTR(hdr_va + XNS_IDP_HEADER_SIZE);

    pkt.header     = hdr_va;
    pkt.data_len   = 0x00990088;        /* only its LOW word is read */
    pkt.mac_src_hi = 0x11223344;
    pkt.mac_src_lo = 0x5566;
    pkt.port_info  = 0x0077;
    for (i = 0; i < 16; i++) {
        pkt.mac_info[i] = (uint8_t)(0xC0 + i);
    }

    app_hdr->src_node = 1;              /* not the direct-return value */
    app_hdr->src_sock = TEST_SOCK;
    app_hdr->net_type = 0;

    port_type = 0x0BAD;
    port_socket = 0x0DAD;
    mac_broadcast = false;
    st = 0x5A5A5A5A;
}

static void call_demux(void)
{
    APP_$DEMUX(&pkt, &port_type, &port_socket, &mac_broadcast, &st);
}

/* ============================================================================
 * The record layout
 * ============================================================================ */

/*
 * Every field the routine writes, checked at its sock_$pkt_info_t offset.
 * A6-0x40 is +0x00, A6-0x3C is +0x04, A6-0x38 is +0x08, A6-0x30 is +0x10,
 * A6-0x2E is +0x12, A6-0x16 is +0x2A, A6-0x14 is +0x2C and A6-0x10 is +0x30.
 */
static void test_record_is_one_sock_pkt_info(void)
{
    const sock_$pkt_info_t *r;
    int i;

    call_demux();

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(1, sock_put_calls, "one SOCK_$PUT");

    r = &sock_put_rec[0];
    ASSERT_EQ(hdr_va, r->hdr, "+0x00 <- descriptor +0x1C");
    ASSERT_EQ(0x11223344u, r->src_addr, "+0x04 <- descriptor +0x26");
    ASSERT_EQ(0x5566, r->src_port, "+0x08 <- descriptor +0x2A");
    ASSERT_EQ(SOCK_PKT_FLAG_XNS, r->flags, "+0x10 is the constant 2");
    ASSERT_EQ(0, r->n_hops, "+0x12 cleared");
    ASSERT_EQ(0x0077, r->data_len, "+0x2A <- descriptor +0x36");
    ASSERT_EQ(0x0088, r->hdr_len, "+0x2C <- the LOW word of descriptor +0x18");

    for (i = 0; i < 16; i++) {
        ASSERT_EQ((uint8_t)(0xC0 + i), ((const uint8_t *)r->data_pages)[i],
                  "+0x30 <- the sixteen bytes at descriptor +0x38");
    }

    /* All of it in one object, not a handful of neighbours. */
    ASSERT_EQ(0x00, offsetof(sock_$pkt_info_t, hdr), "record +0x00");
    ASSERT_EQ(0x10, offsetof(sock_$pkt_info_t, flags), "record +0x10");
    ASSERT_EQ(0x2A, offsetof(sock_$pkt_info_t, data_len), "record +0x2A");
    ASSERT_EQ(0x2C, offsetof(sock_$pkt_info_t, hdr_len), "record +0x2C");
    ASSERT_EQ(0x30, offsetof(sock_$pkt_info_t, data_pages), "record +0x30");
}

/*
 * 0x00E00B16-0x00E00B2E: the socket is the word at the application header's
 * +0x0C, the flag argument is a zero word, and the last two are the words the
 * caller pointed at.
 */
static void test_sock_put_arguments(void)
{
    call_demux();

    ASSERT_EQ(TEST_SOCK, sock_put_sock[0], "the socket from the header");
    ASSERT_EQ(0, sock_put_flags[0], "the flag argument is zero");
    ASSERT_EQ(0x0BAD, sock_put_p4[0], "*port_type");
    ASSERT_EQ(0x0DAD, sock_put_p5[0], "*port_socket");
    ASSERT_EQ(0, rtn_hdr_calls, "a queued packet keeps its buffers");
}

/* 0x00E00AE4 "bset.b #0x2,(-0x2f,A6)" is word bit 2 of the flags at +0x10. */
static void test_broadcast_sets_flag_bit_two(void)
{
    mac_broadcast = true;

    call_demux();

    ASSERT_EQ(SOCK_PKT_FLAG_XNS | SOCK_PKT_FLAG_DEMUX_BOOL,
              sock_put_rec[0].flags, "flags 2 | 4");
}

/* ============================================================================
 * The direct-return condition
 * ============================================================================ */

/*
 * All four tests must hold: +0x08 == 2, +0x0C == 4, +0x14 negative and the
 * caller's boolean TRUE (0x00E00AD4 "tst.b (A1)" / 0x00E00AD6 "bmi").
 */
static void test_direct_return_needs_the_boolean_true(void)
{
    app_hdr->src_node = 2;
    app_hdr->src_sock = 4;
    app_hdr->net_type = 0x80;
    mac_broadcast = true;

    call_demux();

    ASSERT_EQ(0, sock_put_calls, "the packet is never queued");
    ASSERT_EQ(1, rtn_hdr_calls, "its header buffer is returned");
    ASSERT_EQ(hdr_va, rtn_hdr_va, "the header VA");
}

/*
 * The same packet with the boolean FALSE goes down the normal path.  This is
 * the case the old "*flags >= 0" reading got backwards.
 */
static void test_same_packet_with_a_false_boolean_is_queued(void)
{
    app_hdr->src_node = 2;
    app_hdr->src_sock = 4;
    app_hdr->net_type = 0x80;
    mac_broadcast = false;

    call_demux();

    ASSERT_EQ(1, sock_put_calls, "queued as usual");
    ASSERT_EQ(4, sock_put_sock[0], "on the socket the header names");
    ASSERT_EQ(SOCK_PKT_FLAG_XNS, sock_put_rec[0].flags, "no broadcast bit");
}

/* Each of the other three tests, on its own, keeps the packet on the normal
 * path even with the boolean TRUE. */
static void test_direct_return_needs_all_four(void)
{
    mac_broadcast = true;

    setup();
    mac_broadcast = true;
    app_hdr->src_node = 3;              /* not 2 */
    app_hdr->src_sock = 4;
    app_hdr->net_type = 0x80;
    call_demux();
    ASSERT_EQ(1, sock_put_calls, "+0x08 must be 2");

    setup();
    mac_broadcast = true;
    app_hdr->src_node = 2;
    app_hdr->src_sock = 5;              /* not 4 */
    app_hdr->net_type = 0x80;
    call_demux();
    ASSERT_EQ(1, sock_put_calls, "+0x0C must be 4");

    setup();
    mac_broadcast = true;
    app_hdr->src_node = 2;
    app_hdr->src_sock = 4;
    app_hdr->net_type = 0x7F;           /* not negative */
    call_demux();
    ASSERT_EQ(1, sock_put_calls, "+0x14 must be negative");
}

/* ============================================================================
 * The overflow path
 * ============================================================================ */

/*
 * 0x00E00B36-0x00E00B60: a full FILE socket is retried on the overflow
 * socket, counting RING_$FILE_OVERFLOW on the way and
 * RING_$OVERFLOW_OVERFLOW if that fails too.
 */
static void test_file_socket_overflow(void)
{
    app_hdr->src_sock = APP_SOCK_TYPE_FILE;
    sock_put_result[0] = 0;             /* the file socket is full */
    sock_put_result[1] = -1;            /* the overflow socket takes it */

    call_demux();

    ASSERT_EQ(2, sock_put_calls, "two attempts");
    ASSERT_EQ(APP_SOCK_TYPE_FILE, sock_put_sock[0], "first the file socket");
    ASSERT_EQ(APP_SOCK_TYPE_OVERFLOW, sock_put_sock[1], "then the overflow one");
    ASSERT_EQ(1, RING_$FILE_OVERFLOW, "counted");
    ASSERT_EQ(0, RING_$OVERFLOW_OVERFLOW, "not counted");
    ASSERT_EQ(0, rtn_hdr_calls, "the packet was taken, buffers kept");
    /* The SAME record is offered both times (0x00E00B4A "pea (-0x40,A6)"). */
    ASSERT_EQ((uintptr_t)sock_put_rec_ptr[0], (uintptr_t)sock_put_rec_ptr[1],
              "one record, offered twice");
}

static void test_both_sockets_full(void)
{
    app_hdr->src_sock = APP_SOCK_TYPE_FILE;
    pkt.mac_info[0] = 0;                /* data_pages[0] will be non-zero */
    sock_put_result[0] = 0;
    sock_put_result[1] = 0;

    call_demux();

    ASSERT_EQ(2, sock_put_calls, "two attempts");
    ASSERT_EQ(1, RING_$FILE_OVERFLOW, "counted");
    ASSERT_EQ(1, RING_$OVERFLOW_OVERFLOW, "counted");
    ASSERT_EQ(1, rtn_hdr_calls, "the header buffer went back");
    ASSERT_EQ(1, dump_calls, "and so did the data pages");
    ASSERT_EQ(0x0077, dump_len, "PKT_$DUMP_DATA is given the record's +0x2A");
}

/* 0x00E00B36: a non-file socket that refuses the packet is NOT retried. */
static void test_other_socket_full_is_not_retried(void)
{
    sock_put_result[0] = 0;

    call_demux();

    ASSERT_EQ(1, sock_put_calls, "one attempt");
    ASSERT_EQ(0, RING_$FILE_OVERFLOW, "nothing counted");
    ASSERT_EQ(1, rtn_hdr_calls, "the buffers went back");
}

/* 0x00E00B72: with no data pages, PKT_$DUMP_DATA is skipped. */
static void test_no_data_pages_no_dump(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        pkt.mac_info[i] = 0;
    }
    sock_put_result[0] = 0;

    call_demux();

    ASSERT_EQ(1, rtn_hdr_calls, "the header still goes back");
    ASSERT_EQ(0, dump_calls, "but there is nothing to dump");
}

int main(void)
{
    printf("APP_$DEMUX (0x00E00A90) tests\n");

    RUN_TEST(test_record_is_one_sock_pkt_info);
    RUN_TEST(test_sock_put_arguments);
    RUN_TEST(test_broadcast_sets_flag_bit_two);
    RUN_TEST(test_direct_return_needs_the_boolean_true);
    RUN_TEST(test_same_packet_with_a_false_boolean_is_queued);
    RUN_TEST(test_direct_return_needs_all_four);
    RUN_TEST(test_file_socket_overflow);
    RUN_TEST(test_both_sockets_full);
    RUN_TEST(test_other_socket_full_is_not_retried);
    RUN_TEST(test_no_data_pages_no_dump);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
