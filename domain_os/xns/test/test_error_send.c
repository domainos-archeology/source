/*
 * xns/test/test_error_send.c - Unit tests for the two helpers XNS_ERROR_$SEND
 * calls, recovered for bead source-mck5:
 *
 *   xns_$pkt_bufs_in_netbuf_pool (0x00E17876) - the tree used to carry this
 *       under the name xns_$copy_header with a body that returned
 *       packet_info[0x2D].  It actually walks the packet's buffer-descriptor
 *       chain and range-checks every address.
 *   xns_$setup_error_header (0x00E17960) - was an empty stub.  It is a nested
 *       procedure that appends up to 0x2A bytes of the offending packet to the
 *       error packet at offset 0x22.
 *
 * xns/error_send.c is #included below, so both functions under test are the
 * real ones; every callee is stubbed here and records what it was handed.
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
 * Arena: everything the code under test reaches through a 32-bit VA must be
 * carved out of one block that ARCH_HOST_VA_BASE is anchored to.  The VA
 * window the helper range-checks against is [0x00D64C00, 0x00D94C00), so the
 * arena is placed to make VA 0x00D64C00 land at its start.
 * ============================================================================ */

#define POOL_LOW    0x00D64C00u
#define POOL_HIGH   0x00D94C00u

static uint8_t host_arena[0x40000];

static void *va_ptr(uint32_t va)
{
    return ARCH_VA_TO_PTR(va);
}

/* ============================================================================
 * Stubs
 * ============================================================================ */

static int      copy_calls;
static uint32_t copy_src[8];
static uint32_t copy_dst[8];
static uint32_t copy_len[8];

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    if (copy_calls < 8) {
        copy_src[copy_calls] = ARCH_PTR_TO_VA(src);
        copy_dst[copy_calls] = ARCH_PTR_TO_VA(dst);
        copy_len[copy_calls] = len;
    }
    copy_calls++;
    memcpy(dst, src, len);
}

static int      getva_calls;
static uint32_t getva_handle[8];
static uint32_t getva_result[8];
static int      rtnva_calls;
static uint32_t rtnva_va;

void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status)
{
    if (getva_calls < 8) {
        getva_handle[getva_calls] = ppn_shifted;
        *va_out = getva_result[getva_calls];
    } else {
        *va_out = 0;
    }
    getva_calls++;
    *status = status_$ok;
}

uint32_t NETBUF_$RTNVA(uint32_t *va_ptr_in)
{
    rtnva_calls++;
    rtnva_va = *va_ptr_in;
    return 0;
}

void NETBUF_$GET_HDR(uint32_t *pa_out, uint32_t *va_out) { *pa_out = 0; *va_out = 0; }
void NETBUF_$RTN_HDR(uint32_t *va_in) { (void)va_in; }

status_$t FIM_$CLEANUP(void *buf) { (void)buf; return status_$cleanup_handler_set; }
void FIM_$RLS_CLEANUP(void *buf) { (void)buf; }

int8_t xns_$is_local_addr(void *addr) { (void)addr; return 0; }

uint32_t NODE_$ME = 0x0000ABCD;

void XNS_IDP_$OS_OPEN(void *options, status_$t *status_ret) { (void)options; *status_ret = status_$ok; }
void XNS_IDP_$OS_CLOSE(int16_t *channel, status_$t *status_ret) { (void)channel; *status_ret = status_$ok; }
void XNS_IDP_$OS_SEND(int16_t *channel, xns_$os_send_rec_t *send_rec,
                      int16_t *len_sent_ret, status_$t *status_ret)
{ (void)channel; (void)send_rec; (void)len_sent_ret; *status_ret = status_$ok; }

/* The code under test, for real. */
#include "../error_send.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

/*
 * Three descriptors chained through +0x08, the first embedded in the packet
 * record at +0x18.  The payload bytes are distinct per node so the copy
 * order is checkable.
 */
static xns_$pkt_desc_t     pkt;
static mac_os_$buf_desc_t *node1;
static mac_os_$buf_desc_t *node2;
static uint32_t            node1_va, node2_va;
static uint32_t            buf0_va, buf1_va, buf2_va, buf3_va;
static uint32_t            hdrbuf_va;

static void setup(void)
{
    uint8_t *p;
    int i;

    memset(host_arena, 0, sizeof(host_arena));
    memset(&pkt, 0, sizeof(pkt));
    copy_calls = 0;
    getva_calls = 0;
    rtnva_calls = 0;
    rtnva_va = 0;
    for (i = 0; i < 8; i++) {
        getva_result[i] = 0;
    }

    /* VA POOL_LOW maps to host_arena[0]. */
    ARCH_HOST_VA_BASE = (uintptr_t)host_arena - (uintptr_t)POOL_LOW;

    node1_va  = POOL_LOW + 0x0100;
    node2_va  = POOL_LOW + 0x0200;
    buf0_va   = POOL_LOW + 0x1000;
    buf1_va   = POOL_LOW + 0x2000;
    buf2_va   = POOL_LOW + 0x3000;
    buf3_va   = POOL_LOW + 0x4000;
    hdrbuf_va = POOL_LOW + 0x5000;

    node1 = (mac_os_$buf_desc_t *)va_ptr(node1_va);
    node2 = (mac_os_$buf_desc_t *)va_ptr(node2_va);

    /* Head descriptor, embedded at packet record +0x18. */
    pkt.data_len = 0x10;
    pkt.header   = (xns_$idp_header_t *)va_ptr(buf0_va);
    pkt.iov      = node1_va;

    node1->length  = 0x08;
    node1->address = buf1_va;
    node1->next    = node2_va;

    node2->length  = 0x40;
    node2->address = buf2_va;
    node2->next    = 0;

    p = (uint8_t *)va_ptr(buf0_va);
    for (i = 0; i < 0x40; i++) { p[i] = (uint8_t)(0x10 + i); }
    p = (uint8_t *)va_ptr(buf1_va);
    for (i = 0; i < 0x40; i++) { p[i] = (uint8_t)(0x50 + i); }
    p = (uint8_t *)va_ptr(buf2_va);
    for (i = 0; i < 0x40; i++) { p[i] = (uint8_t)(0x90 + i); }
    p = (uint8_t *)va_ptr(buf3_va);
    for (i = 0; i < 0x40; i++) { p[i] = (uint8_t)(0xD0 + i); }
}

static status_$t frame_status;

static void make_frame(xns_$error_send_frame_t *f, boolean in_pool)
{
    frame_status      = status_$ok;
    f->packet_info    = &pkt;
    f->packet_info_2  = &pkt;
    f->status_ret     = &frame_status;
    f->netbuf_va      = hdrbuf_va;
    f->remaining      = 0;
    f->in_netbuf_pool = in_pool;
}

/* ============================================================================
 * xns_$pkt_bufs_in_netbuf_pool (0x00E17876)
 * ============================================================================ */

/*
 * All three descriptor addresses sit inside [POOL_LOW, POOL_HIGH), so the
 * `clr.b D0b` at 0x00E17898 is never reached and the initial `st D0b`
 * (0x00E1787E) survives to the return.
 */
static void test_all_addresses_in_window(void)
{
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)xns_$pkt_bufs_in_netbuf_pool(&pkt),
              "every descriptor inside the window");
}

/*
 * The chain head is the record's OWN {data_len, header, iov} triple at +0x18
 * (0x00E17880 `lea (0x18,A0),A0`), so an out-of-window `header` is enough to
 * fail even though the two linked nodes are fine.
 */
static void test_head_descriptor_is_checked(void)
{
    pkt.header = (xns_$idp_header_t *)va_ptr(POOL_LOW - 4);
    ASSERT_EQ(0x00, (uint8_t)xns_$pkt_bufs_in_netbuf_pool(&pkt),
              "the embedded head descriptor counts");
}

/* 0x00E1788C `cmp.l (0x70,A5),D1` / `blt` - strictly below the low bound. */
static void test_address_below_window(void)
{
    node1->address = POOL_LOW - 1;
    ASSERT_EQ(0x00, (uint8_t)xns_$pkt_bufs_in_netbuf_pool(&pkt), "one below the low bound");
    node1->address = POOL_LOW;
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)xns_$pkt_bufs_in_netbuf_pool(&pkt),
              "the low bound itself is inside");
}

/* 0x00E17892 `cmp.l (0x6c,A5),D1` / `blt` to "next" - the high bound is out. */
static void test_address_at_or_above_window(void)
{
    node2->address = POOL_HIGH;
    ASSERT_EQ(0x00, (uint8_t)xns_$pkt_bufs_in_netbuf_pool(&pkt), "the high bound is outside");
    node2->address = POOL_HIGH - 1;
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)xns_$pkt_bufs_in_netbuf_pool(&pkt),
              "one below the high bound is inside");
}

/* 0x00E178A0 `cmpa.w #0x0,A0` - a zero link ends the walk. */
static void test_walk_stops_at_a_null_link(void)
{
    pkt.iov = 0;
    node1->address = POOL_HIGH + 0x1000;   /* never looked at */
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)xns_$pkt_bufs_in_netbuf_pool(&pkt),
              "nodes past the null link are not examined");
}

/* ============================================================================
 * xns_$setup_error_header (0x00E17960)
 * ============================================================================ */

/*
 * With in_netbuf_pool false the descriptor chain is simply walked.
 * remaining starts at 0x2A (0x00E1796C) and the destination offset at 0x22
 * (0x00E17972); each copy takes min(remaining, node->length) bytes.
 * 0x10 + 0x08 + 0x12 = 0x2A, so the third node is clamped to 0x12 (its own
 * length is 0x40) and the walk stops with remaining == 0.
 */
static void test_chain_walk_copies_and_clamps(void)
{
    xns_$error_send_frame_t f;
    const uint8_t *out;

    make_frame(&f, false);
    xns_$setup_error_header(&f);

    ASSERT_EQ(3, copy_calls, "one copy per descriptor");
    ASSERT_EQ(buf0_va, copy_src[0], "first source is the head descriptor");
    ASSERT_EQ(hdrbuf_va + 0x22, copy_dst[0], "first destination is header + 0x22");
    ASSERT_EQ(0x10, copy_len[0], "first length is the head's own length");

    ASSERT_EQ(buf1_va, copy_src[1], "second source");
    ASSERT_EQ(hdrbuf_va + 0x32, copy_dst[1], "destination advanced by 0x10");
    ASSERT_EQ(0x08, copy_len[1], "second length");

    ASSERT_EQ(buf2_va, copy_src[2], "third source");
    ASSERT_EQ(hdrbuf_va + 0x3A, copy_dst[2], "destination advanced by 0x18");
    ASSERT_EQ(0x12, copy_len[2], "third length clamped to what is left of 0x2A");

    ASSERT_EQ(0, f.remaining, "the whole 0x2A window was filled");
    ASSERT_EQ(0, rtnva_calls, "no netbuf page was ever fetched");

    out = (const uint8_t *)va_ptr(hdrbuf_va);
    ASSERT_EQ(0x10, out[0x22], "first payload byte landed at +0x22");
    ASSERT_EQ(0x50, out[0x32], "second node's payload follows");
    ASSERT_EQ(0x90, out[0x3A], "third node's payload follows");
    ASSERT_EQ(0x00, out[0x21], "nothing was written below +0x22");
    ASSERT_EQ(0x00, out[0x4C], "nothing was written past +0x22+0x2A");
}

/*
 * 0x00E179EC `cmpa.w #0x0,A3` / 0x00E179F2 `clr.l D4`: when the chain runs
 * out before 0x2A bytes have been copied, D4 goes to zero and the loop test
 * at 0x00E17A0C ends the walk with remaining still positive.
 */
static void test_short_chain_leaves_remaining_positive(void)
{
    xns_$error_send_frame_t f;

    node1->next = 0;
    make_frame(&f, false);
    xns_$setup_error_header(&f);

    ASSERT_EQ(2, copy_calls, "only the two descriptors that exist");
    ASSERT_EQ(0x2A - 0x18, f.remaining, "0x2A minus 0x10 minus 0x08");
    ASSERT_EQ(0, rtnva_calls, "nothing to return");
}

/*
 * With in_netbuf_pool TRUE (0x00E179AC `tst.b (-0x3c,A2)` / `bpl` skips the
 * fetch when the byte is NOT negative), the payload is paged in with
 * NETBUF_$GETVA using the packet record's +0x38 handle, and the length is
 * clamped against its +0x34.  The last page fetched is handed back with
 * NETBUF_$RTNVA (0x00E17A1E).
 */
static void test_netbuf_pool_path_pages_and_returns(void)
{
    xns_$error_send_frame_t f;
    const uint8_t *out;

    pkt.netbuf_len    = 0x0C;
    pkt.netbuf_handle = 0xABCD1234u;
    getva_result[0]   = buf3_va;
    getva_result[1]   = buf3_va;
    getva_result[2]   = buf3_va;

    make_frame(&f, true);
    xns_$setup_error_header(&f);

    /* head 0x10 leaves 0x1A, then 0x0C + 0x0C + 0x02 out of the netbuf pages. */
    ASSERT_EQ(4, copy_calls, "head plus three paged copies");
    ASSERT_EQ(buf0_va, copy_src[0], "the head descriptor is still used first");
    ASSERT_EQ(0x10, copy_len[0], "head length");
    ASSERT_EQ(buf3_va, copy_src[1], "the fetched page");
    ASSERT_EQ(0x0C, copy_len[1], "clamped to netbuf_len");
    ASSERT_EQ(0x0C, copy_len[2], "clamped again");
    ASSERT_EQ(0x02, copy_len[3], "final partial chunk");

    ASSERT_EQ(3, getva_calls, "one fetch per paged copy");
    ASSERT_EQ(0xABCD1234u, getva_handle[0], "the handle is packet record +0x38");

    ASSERT_EQ(0, f.remaining, "the whole 0x2A window was filled");
    ASSERT_EQ(1, rtnva_calls, "exactly one NETBUF_$RTNVA on the way out");
    ASSERT_EQ(buf3_va, rtnva_va, "the last page fetched is the one returned");

    out = (const uint8_t *)va_ptr(hdrbuf_va);
    ASSERT_EQ(0xD0, out[0x32], "the paged bytes follow the head's");
}

/*
 * 0x00E17A12 `tst.l D2` / `beq`: if the netbuf path was never taken there is
 * nothing to give back, even when in_netbuf_pool is true.
 */
static void test_no_rtnva_when_the_first_copy_finishes_the_window(void)
{
    xns_$error_send_frame_t f;

    pkt.data_len = 0x2A;            /* the head alone fills the window */
    make_frame(&f, true);
    xns_$setup_error_header(&f);

    ASSERT_EQ(1, copy_calls, "one copy");
    ASSERT_EQ(0x2A, copy_len[0], "clamped to the 0x2A window");
    ASSERT_EQ(0, getva_calls, "the remaining==0 exit comes first (0x00E179A6)");
    ASSERT_EQ(0, rtnva_calls, "nothing was fetched, nothing is returned");
}

/*
 * A zero address in the head descriptor makes the loop test at 0x00E17A0C
 * fail immediately, so nothing is copied and remaining stays at 0x2A - which
 * is what XNS_ERROR_$SEND turns into a 0x22-byte packet.
 */
static void test_zero_head_address_copies_nothing(void)
{
    xns_$error_send_frame_t f;

    pkt.header = NULL;
    make_frame(&f, false);
    xns_$setup_error_header(&f);

    ASSERT_EQ(0, copy_calls, "no copy at all");
    ASSERT_EQ(0x2A, f.remaining, "remaining untouched");
    ASSERT_EQ(0x22, 0x4C - f.remaining, "XNS_ERROR_$SEND would send 0x22 bytes");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("xns error-send helper tests\n");

    RUN_TEST(test_all_addresses_in_window);
    RUN_TEST(test_head_descriptor_is_checked);
    RUN_TEST(test_address_below_window);
    RUN_TEST(test_address_at_or_above_window);
    RUN_TEST(test_walk_stops_at_a_null_link);
    RUN_TEST(test_chain_walk_copies_and_clamps);
    RUN_TEST(test_short_chain_leaves_remaining_positive);
    RUN_TEST(test_netbuf_pool_path_pages_and_returns);
    RUN_TEST(test_no_rtnva_when_the_first_copy_finishes_the_window);
    RUN_TEST(test_zero_head_address_copies_nothing);

    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
