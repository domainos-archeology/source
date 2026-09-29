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

static int      get_hdr_calls;
static uint32_t get_hdr_pa;
static uint32_t get_hdr_va;
static int      rtn_hdr_calls;
static uint32_t rtn_hdr_va;

void NETBUF_$GET_HDR(uint32_t *pa_out, uint32_t *va_out)
{
    get_hdr_calls++;
    *pa_out = get_hdr_pa;
    *va_out = get_hdr_va;
}

void NETBUF_$RTN_HDR(uint32_t *va_in) { rtn_hdr_calls++; rtn_hdr_va = *va_in; }

static int   excl_start_calls;
static int   excl_stop_calls;
static void *excl_last;

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { excl_start_calls++; excl_last = e; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { excl_stop_calls++;  excl_last = e; }

static status_$t cleanup_result;
static int       rls_cleanup_calls;

status_$t FIM_$CLEANUP(void *buf) { (void)buf; return cleanup_result; }
void FIM_$RLS_CLEANUP(void *buf) { (void)buf; rls_cleanup_calls++; }

static int    is_local_calls;
static void  *is_local_arg[4];
static int8_t is_local_result[4];

int8_t xns_$is_local_addr(void *addr)
{
    int8_t answer = (is_local_calls < 4) ? is_local_result[is_local_calls] : 0;

    if (is_local_calls < 4) {
        is_local_arg[is_local_calls] = addr;
    }
    is_local_calls++;
    return answer;
}

uint32_t NODE_$ME = 0x0000ABCD;

static int                os_open_calls;
static xns_$os_open_opt_t os_open_seen;
static uint16_t           os_open_channel;
static status_$t          os_open_status;

void XNS_IDP_$OS_OPEN(xns_$os_open_opt_t *options, status_$t *status_ret)
{
    os_open_calls++;
    os_open_seen = *options;
    options->flags_channel = os_open_channel;
    *status_ret = os_open_status;
}

static int     os_close_calls;
static int16_t os_close_channel;

void XNS_IDP_$OS_CLOSE(int16_t *channel, status_$t *status_ret)
{
    os_close_calls++;
    os_close_channel = *channel;
    *status_ret = status_$ok;
}

static int                 os_send_calls;
static const int16_t      *os_send_channel;
static xns_$os_send_rec_t *os_send_rec;
static xns_$os_send_rec_t  os_send_rec_copy;

void XNS_IDP_$OS_SEND(int16_t *channel, xns_$os_send_rec_t *send_rec,
                      int16_t *len_sent_ret, status_$t *status_ret)
{
    os_send_calls++;
    os_send_channel = channel;
    os_send_rec = send_rec;
    os_send_rec_copy = *send_rec;
    *len_sent_ret = 0;
    *status_ret = status_$ok;
}

/* The XNS_ERROR module block the code under test works on (its image
 * contents are in xns/xns_data.c; setup() below sets them). */
MODULE_DATA_DEFINE(xns_error_$data_t, XNS_ERROR_$DATA, 0x00E2B29C);

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
    get_hdr_calls = 0;
    rtn_hdr_calls = 0;
    excl_start_calls = 0;
    excl_stop_calls = 0;
    is_local_calls = 0;
    is_local_result[0] = 0;
    is_local_result[1] = 0;
    is_local_result[2] = 0;
    is_local_result[3] = 0;
    os_open_calls = 0;
    os_open_channel = 0x0007;
    os_open_status = status_$ok;
    os_close_calls = 0;
    os_send_calls = 0;
    rls_cleanup_calls = 0;
    cleanup_result = status_$cleanup_handler_set;

    /* The module data starts each test as the image has it. */
    memset(&XNS_ERROR_$DATA, 0, sizeof(XNS_ERROR_$DATA));
    XNS_ERROR_$DATA.buf_va_high = 0x00D94C00;
    XNS_ERROR_$DATA.buf_va_low  = 0x00D64C00;
    XNS_ERROR_$DATA.client_ref_count = 0;
    XNS_ERROR_$DATA.std_idp_channel = -1;
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
    pkt.header   = buf0_va;
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

    get_hdr_pa = 0x00001234;            /* a non-zero netbuf handle */
    get_hdr_va = hdrbuf_va;
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
    pkt.header = POOL_LOW - 4;
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

    pkt.header = 0;
    make_frame(&f, false);
    xns_$setup_error_header(&f);

    ASSERT_EQ(0, copy_calls, "no copy at all");
    ASSERT_EQ(0x2A, f.remaining, "remaining untouched");
    ASSERT_EQ(0x22, 0x4C - f.remaining, "XNS_ERROR_$SEND would send 0x22 bytes");
}

/* ============================================================================
 * XNS_ERROR_$SEND itself (0x00E17A2E) - bead source-ga8r
 * ============================================================================ */

static uint16_t err_code;
static uint16_t err_param;
static uint16_t err_result;
static status_$t err_status;

/*
 * The offending packet has to look plausible: its descriptor must claim at
 * least the 0x1E bytes of an IDP header (0x00E17A74) and carry a non-zero
 * header address (0x00E17A7E).  Making the head long enough for the whole
 * 0x2A window keeps the copy to a single call.
 */
static void make_offending_packet(void)
{
    pkt.data_len = 0x40;
    pkt.header = buf0_va;
    pkt.iov = 0;
    /* Not an error packet itself (0x00E17AC4 reads the header's +0x05). */
    ((uint8_t *)va_ptr(buf0_va))[5] = 1;
}

static void call_send(void)
{
    err_code = 0x0202;
    err_param = 0x0011;
    err_result = 0xFFFF;
    err_status = 0x5A5A5A5A;
    XNS_ERROR_$SEND(&pkt, &err_code, &err_param, &err_result, &err_status);
}

/*
 * 0x00E17B3C-0x00E17BA0, every field of the error packet at its own offset.
 * The address swap at 0x00E17B52 copies the packet's +0x34 - which is
 * orig[0x12], the offending packet's IDP SOURCE address - onto +0x06.
 */
static void test_error_packet_header_offsets(void)
{
    const uint8_t *out;
    const uint8_t *orig;
    const xns_$error_pkt_t *epkt;
    int i;

    make_offending_packet();
    call_send();

    ASSERT_EQ(status_$ok, err_status, "status");
    ASSERT_EQ(1, os_send_calls, "the packet was sent");

    out = (const uint8_t *)va_ptr(hdrbuf_va);
    orig = (const uint8_t *)va_ptr(buf0_va);
    epkt = (const xns_$error_pkt_t *)va_ptr(hdrbuf_va);

    /*
     * The scalar fields are read through the record, not byte by byte: the
     * tree models a wire header's integers as native integers, so only the
     * genuinely byte-sized fields and the byte-copied address blocks have a
     * fixed order on a little-endian host.
     */
    ASSERT_EQ(0xFFFF, epkt->idp.checksum, "checksum (0x00E17B3E move.w #-1)");
    ASSERT_EQ(0x4C, epkt->idp.length,
              "length - the whole 0x4C window was used (0x00E17B42)");
    ASSERT_EQ(0x00, out[0x04], "transport control (0x00E17B48)");
    ASSERT_EQ(0x03, out[0x05], "packet type 3 (0x00E17B4C)");

    /* 0x00E17B52: twelve bytes from the packet's own +0x34 to its +0x06. */
    for (i = 0; i < 12; i++) {
        ASSERT_EQ(orig[0x12 + i], out[0x06 + i],
                  "destination address is the offending source address");
        ASSERT_EQ(out[0x34 + i], out[0x06 + i], "copied from +0x34");
    }

    ASSERT_EQ(0x00, out[0x12], "source network cleared (0x00E17B66)");
    ASSERT_EQ(0x00, out[0x13], "source network cleared");
    ASSERT_EQ(0x00, out[0x14], "source network cleared");
    ASSERT_EQ(0x00, out[0x15], "source network cleared");

    /* 0x00E17B6A-0x00E17B8E, NODE_$ME == 0x0000ABCD. */
    ASSERT_EQ(0x08, out[0x16], "source host 08:00:1E:..");
    ASSERT_EQ(0x00, out[0x17], "source host");
    ASSERT_EQ(0x1E, out[0x18], "((NODE_$ME >> 16) & 0xF) | 0x1E00, high byte");
    ASSERT_EQ(0x00, out[0x19], "low byte");
    ASSERT_EQ(0xAB, out[0x1A], "the low word of NODE_$ME");
    ASSERT_EQ(0xCD, out[0x1B], "the low word of NODE_$ME");

    ASSERT_EQ(XNS_SOCKET_ERROR, epkt->idp.src_socket,
              "source socket 3 at +0x1C (0x00E17B60)");
    ASSERT_EQ(0x1C, offsetof(xns_$error_pkt_t, idp.src_socket),
              "and the cell really is +0x1C, not +0x0E");

    ASSERT_EQ(0x0202, epkt->error_code, "the error CODE (0x00E17B9E)");
    ASSERT_EQ(0x1E, offsetof(xns_$error_pkt_t, error_code), "at +0x1E");
    ASSERT_EQ(0x0011, epkt->error_param, "the error PARAMETER (0x00E17B96)");
    ASSERT_EQ(0x20, offsetof(xns_$error_pkt_t, error_param), "at +0x20");

    /* 0x00E17B2C-0x00E17B38: the request record is the module data itself. */
    ASSERT_EQ((uintptr_t)&XNS_ERROR_$DATA.send_rec, (uintptr_t)os_send_rec,
              "the record handed to XNS_IDP_$OS_SEND is at A5+0x00");
    ASSERT_EQ(0x4C, os_send_rec_copy.hdr_desc.length, "descriptor length");
    ASSERT_EQ(hdrbuf_va, os_send_rec_copy.hdr_desc.address, "descriptor address");
    ASSERT_EQ(0, os_send_rec_copy.hdr_desc.next, "descriptor end of chain");
    ASSERT_EQ((uint8_t)true, (uint8_t)os_send_rec_copy.hdr_prebuilt,
              "the header is already built");
    ASSERT_EQ((uintptr_t)&XNS_ERROR_$DATA.std_idp_channel,
              (uintptr_t)os_send_channel,
              "the channel argument is the cell at A5+0x76");
    ASSERT_EQ(0, err_result, "the result word was cleared and reported");
}

/*
 * 0x00E17B22-0x00E17B2C: the length is 0x4C minus whatever
 * xns_$setup_error_header could NOT append.
 */
static void test_short_packet_shortens_the_error_packet(void)
{
    const xns_$error_pkt_t *epkt;

    make_offending_packet();
    pkt.data_len = 0x20;                /* only 0x20 bytes to append */
    call_send();

    epkt = (const xns_$error_pkt_t *)va_ptr(hdrbuf_va);
    ASSERT_EQ(0x22 + 0x20, os_send_rec_copy.hdr_desc.length,
              "0x4C minus the 0x0A that were never copied");
    ASSERT_EQ(0x42, epkt->idp.length, "and the IDP length word agrees");
}

/*
 * 0x00E178AA-0x00E1790E and 0x00E17910-0x00E1795E: the channel is opened on
 * the first client and closed on the last, both under
 * XNS_ERROR_$CLIENT_MUTEX.
 */
static void test_error_socket_is_reference_counted(void)
{
    make_offending_packet();
    call_send();

    ASSERT_EQ(1, os_open_calls, "the channel was opened");
    ASSERT_EQ(XNS_SOCKET_ERROR, os_open_seen.socket, "socket 3");
    ASSERT_EQ(XNS_OPEN_FLAG_NO_ALLOC, os_open_seen.flags_channel,
              "the flag word is 0x0008");
    ASSERT_EQ(0, os_open_seen.demux, "no demux vector");
    ASSERT_EQ(1, os_close_calls, "and closed again");
    ASSERT_EQ(0x0007, os_close_channel, "the channel XNS_IDP_$OS_OPEN gave");
    ASSERT_EQ(0, XNS_ERROR_$DATA.client_ref_count, "back to no clients");
    ASSERT_EQ(-1, XNS_ERROR_$DATA.std_idp_channel, "and the cell is reset");
    ASSERT_EQ(2, excl_start_calls, "the mutex was taken twice");
    ASSERT_EQ(2, excl_stop_calls, "and released twice");
    ASSERT_EQ((uintptr_t)&XNS_ERROR_$CLIENT_MUTEX, (uintptr_t)excl_last,
              "it is XNS_ERROR_$CLIENT_MUTEX");
    ASSERT_EQ(1, rls_cleanup_calls, "the cleanup handler was released");
}

/*
 * 0x00E178CC / 0x00E178F6: a second client while one is already inside does
 * NOT reopen, but does bump the count.
 */
static void test_second_client_does_not_reopen(void)
{
    XNS_ERROR_$DATA.client_ref_count = 1;
    XNS_ERROR_$DATA.std_idp_channel = 0x0042;

    make_offending_packet();
    call_send();

    ASSERT_EQ(0, os_open_calls, "no second open");
    ASSERT_EQ(0, os_close_calls, "and no close either");
    ASSERT_EQ(1, XNS_ERROR_$DATA.client_ref_count, "up then down again");
    ASSERT_EQ(0x0042, XNS_ERROR_$DATA.std_idp_channel, "the channel is kept");
}

/* 0x00E178EC: a failed open reports the status and skips the increment. */
static void test_failed_open_is_reported(void)
{
    os_open_status = status_$xns_channel_table_full;

    make_offending_packet();
    call_send();

    ASSERT_EQ(status_$xns_channel_table_full, err_status, "status");
    ASSERT_EQ(0, XNS_ERROR_$DATA.client_ref_count, "the count did not move");
    ASSERT_EQ(0, os_send_calls, "nothing was sent");
    ASSERT_EQ(0, get_hdr_calls, "no header buffer was taken");
    ASSERT_EQ(0, rls_cleanup_calls,
              "and the cleanup handler is left registered (0x00E17AF4)");
}

/* 0x00E17A74 / 0x00E17A7E: the offending packet has to be big enough and
 * have a header. */
static void test_undersized_or_headerless_packet(void)
{
    make_offending_packet();
    pkt.data_len = XNS_IDP_HEADER_SIZE - 1;
    call_send();
    ASSERT_EQ(status_$xns_error_illegal_buffer_spec, err_status, "too short");
    ASSERT_EQ(0, os_open_calls, "nothing was opened");

    setup();
    make_offending_packet();
    pkt.header = 0;
    call_send();
    ASSERT_EQ(status_$xns_error_illegal_buffer_spec, err_status, "no header");
}

/*
 * 0x00E17A94 / 0x00E17AA2: the SOURCE address at the header's +0x12 and the
 * DESTINATION at +0x06 are both examined, and either being a broadcast is
 * refused.
 */
static void test_broadcast_source_or_destination(void)
{
    make_offending_packet();
    is_local_result[0] = -1;
    call_send();
    ASSERT_EQ(status_$xns_error_source_is_broadcast, err_status, "source");
    ASSERT_EQ(2, is_local_calls, "both calls always run");
    ASSERT_EQ((uintptr_t)((const uint8_t *)va_ptr(buf0_va) + 0x12),
              (uintptr_t)is_local_arg[0], "first argument is header + 0x12");
    ASSERT_EQ((uintptr_t)((const uint8_t *)va_ptr(buf0_va) + 0x06),
              (uintptr_t)is_local_arg[1], "second argument is header + 0x06");

    setup();
    make_offending_packet();
    is_local_result[1] = -1;
    call_send();
    ASSERT_EQ(status_$xns_error_source_is_broadcast, err_status, "destination");
}

/* 0x00E17AC4: an error packet never gets an error packet back. */
static void test_error_packets_are_not_answered(void)
{
    make_offending_packet();
    ((uint8_t *)va_ptr(buf0_va))[5] = XNS_IDP_TYPE_ERROR;
    call_send();

    ASSERT_EQ(status_$xns_error_packet_type_error, err_status, "status");
    ASSERT_EQ(0, os_open_calls, "nothing was opened");
}

/*
 * 0x00E17BCA-0x00E17BEA: when FIM_$CLEANUP reports anything but
 * status_$cleanup_handler_set the routine unwinds - it hands back the header
 * buffer if it had one and closes the error socket if it had opened one -
 * then reports what FIM_$CLEANUP said.
 *
 * Only the status can be observed from C: the two flags this arm consults
 * are locals that are still at their entry values when the handler was never
 * armed in the first place.
 */
static void test_cleanup_handler_reports_its_status(void)
{
    cleanup_result = status_$xns_no_data;   /* any non-"handler set" code */

    make_offending_packet();
    call_send();

    ASSERT_EQ(status_$xns_no_data, err_status, "the status is passed through");
    ASSERT_EQ(0, os_send_calls, "nothing was sent");
    ASSERT_EQ(0, rtn_hdr_calls, "no header had been taken yet");
    ASSERT_EQ(0, os_close_calls, "and no socket had been opened yet");
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

    RUN_TEST(test_error_packet_header_offsets);
    RUN_TEST(test_short_packet_shortens_the_error_packet);
    RUN_TEST(test_error_socket_is_reference_counted);
    RUN_TEST(test_second_client_does_not_reopen);
    RUN_TEST(test_failed_open_is_reported);
    RUN_TEST(test_undersized_or_headerless_packet);
    RUN_TEST(test_broadcast_source_or_destination);
    RUN_TEST(test_error_packets_are_not_answered);
    RUN_TEST(test_cleanup_handler_reports_its_status);

    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
