/*
 * xns/test/test_idp_receive.c - XNS_IDP_$RECEIVE (0x00E18CE2), bead source-pvhv
 *
 * Two things the bead names:
 *
 *   - xns_$copy_packet_data (0x00E18C5E) is a nested procedure that fills the
 *     caller's buffer chain, carrying its position between calls in the
 *     parent's A6-0x5C (the current descriptor) and A6-0x76 (how far into it
 *     the last copy reached).  It used to be an empty stub.
 *   - the parent omitted 0x00E18E8C-0x00E18E9E: when a descriptor was only
 *     partly filled its length is rewritten with the byte count, and the
 *     cursor advances, BEFORE the loop that zeroes the rest.
 *
 * xns/idp_receive.c is #included below so both functions under test are the
 * real ones; every callee is stubbed here.
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
 * Globals
 * ============================================================================ */

static uint8_t idp_arena[0x2000];
uint8_t *XNS_IDP_BASE = idp_arena;

uint16_t PROC1_$AS_ID;

/*
 * Everything the code reaches through a target VA - packet buffers, the
 * caller's receive buffers and the descriptors that link them.
 */
#define TARGET_VA_BASE 0x00100000u
static uint8_t target_arena[0x4000];

static void *va_ptr(uint32_t va) { return ARCH_VA_TO_PTR(va); }

static uint32_t ptr_to_va(const void *p)
{
    return (uint32_t)((const uint8_t *)p - target_arena) + TARGET_VA_BASE;
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
        copy_src[copy_calls] = ptr_to_va(src);
        copy_dst[copy_calls] = ptr_to_va(dst);
        copy_len[copy_calls] = len;
    }
    copy_calls++;
    memcpy(dst, src, len);
}

static int              sock_get_calls;
static uint16_t         sock_get_sock;
static int8_t           sock_get_result;
static sock_$pkt_info_t sock_get_rec;

int8_t SOCK_$GET(uint16_t sock_num, void *pkt_info)
{
    sock_get_calls++;
    sock_get_sock = sock_num;
    memcpy(pkt_info, &sock_get_rec, sizeof(sock_get_rec));
    return sock_get_result;
}

static int       getva_calls;
static uint32_t  getva_handle;
static uint32_t  getva_result;
static status_$t getva_status;

void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status)
{
    getva_calls++;
    getva_handle = ppn_shifted;
    *va_out = getva_result;
    *status = getva_status;
}

static int      rtn_pkt_calls;
static uint32_t rtn_pkt_hdr;
static uint32_t rtn_pkt_va;
static int16_t  rtn_pkt_len;

void NETBUF_$RTN_PKT(uint32_t *hdr_ptr, uint32_t *va_ptr_in,
                     uint32_t *dat_arr, int16_t dat_len)
{
    (void)dat_arr;
    rtn_pkt_calls++;
    rtn_pkt_hdr = *hdr_ptr;
    rtn_pkt_va = *va_ptr_in;
    rtn_pkt_len = dat_len;
}

static status_$t cleanup_result;
static int       rls_cleanup_calls;

status_$t FIM_$CLEANUP(void *buf) { (void)buf; return cleanup_result; }
void FIM_$RLS_CLEANUP(void *buf) { (void)buf; rls_cleanup_calls++; }

/* The code under test, for real. */
#include "../idp_receive.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

#define TEST_CHANNEL      2
#define TEST_USER_SOCKET  0x0021

static xns_$idp_recv_t recv_rec;
static status_$t st;

/* Three buffers the caller offers, chained through the record's own head. */
static mac_os_$buf_desc_t *iov1;
static mac_os_$buf_desc_t *iov2;
static uint32_t iov1_va, iov2_va;
static uint32_t buf0_va, buf1_va, buf2_va;
static uint32_t hdr_va, data_va;

static uint8_t *chan;

static void setup(void)
{
    uint8_t *p;
    int i;

    memset(idp_arena, 0, sizeof(idp_arena));
    memset(target_arena, 0, sizeof(target_arena));
    memset(&recv_rec, 0, sizeof(recv_rec));
    memset(&sock_get_rec, 0, sizeof(sock_get_rec));

    copy_calls = 0;
    sock_get_calls = 0;
    sock_get_result = -1;               /* a packet is waiting */
    getva_calls = 0;
    getva_status = status_$ok;
    rtn_pkt_calls = 0;
    rls_cleanup_calls = 0;
    cleanup_result = status_$cleanup_handler_set;
    PROC1_$AS_ID = 3;

    ARCH_HOST_VA_BASE = (uintptr_t)target_arena - (uintptr_t)TARGET_VA_BASE;

    chan = idp_arena + TEST_CHANNEL * XNS_CHANNEL_SIZE;
    *(uint16_t *)(chan + XNS_CHAN_OFF_STATE) = 0x8000;          /* active */
    *(uint16_t *)(chan + XNS_CHAN_OFF_FLAGS) =
        (uint16_t)(3 << XNS_CHAN_FLAG_AS_ID_SHIFT);             /* our AS_ID */
    *(uint16_t *)(chan + XNS_CHAN_OFF_USER_SOCKET) = TEST_USER_SOCKET;

    iov1_va = TARGET_VA_BASE + 0x0100;
    iov2_va = TARGET_VA_BASE + 0x0200;
    buf0_va = TARGET_VA_BASE + 0x1000;
    buf1_va = TARGET_VA_BASE + 0x1100;
    buf2_va = TARGET_VA_BASE + 0x1200;
    hdr_va  = TARGET_VA_BASE + 0x2000;
    data_va = TARGET_VA_BASE + 0x2100;

    iov1 = (mac_os_$buf_desc_t *)va_ptr(iov1_va);
    iov2 = (mac_os_$buf_desc_t *)va_ptr(iov2_va);

    /* The head descriptor is embedded in the caller's record at +0x18. */
    recv_rec.iov.length  = 0x10;
    recv_rec.iov.address = buf0_va;
    recv_rec.iov.next    = iov1_va;

    iov1->length  = 0x10;
    iov1->address = buf1_va;
    iov1->next    = iov2_va;

    iov2->length  = 0x10;
    iov2->address = buf2_va;
    iov2->next    = 0;

    /* The packet SOCK_$GET reports: 0x0C header bytes and 0x0C data bytes. */
    sock_get_rec.hdr = hdr_va;
    sock_get_rec.src_addr = 0xAABBCCDD;
    sock_get_rec.src_port = 0x1234;
    sock_get_rec.hdr_len = 0x0C;
    sock_get_rec.data_len = 0x0C;
    sock_get_rec.data_pages[0] = 0x00007000;
    getva_result = data_va;

    p = (uint8_t *)va_ptr(hdr_va);
    for (i = 0; i < 0x40; i++) { p[i] = (uint8_t)(0x10 + i); }
    p = (uint8_t *)va_ptr(data_va);
    for (i = 0; i < 0x40; i++) { p[i] = (uint8_t)(0x80 + i); }

    st = 0x5A5A5A5A;
}

static uint16_t channel_num = TEST_CHANNEL;

static void call_receive(void)
{
    channel_num = TEST_CHANNEL;
    XNS_IDP_$RECEIVE(&channel_num, &recv_rec, &st);
}

/* ============================================================================
 * The nested copy and the partial-iov block
 * ============================================================================ */

/*
 * 0x0C + 0x0C = 0x18 bytes into three 0x10-byte descriptors: the first is
 * filled, the second is left half full and its length is rewritten to 8 by
 * 0x00E18E9C, and the third is zeroed by the loop at 0x00E18EA0.
 */
static void test_partial_descriptor_is_shortened(void)
{
    const uint8_t *out;

    call_receive();

    ASSERT_EQ(status_$ok, st, "status");

    /* 0x00E18E60/0x00E18E76: one call per non-empty half of the packet. */
    ASSERT_EQ(3, copy_calls, "header fills the head, data spans two");
    ASSERT_EQ(hdr_va, copy_src[0], "the header VA");
    ASSERT_EQ(buf0_va, copy_dst[0], "into the head descriptor");
    ASSERT_EQ(0x0C, copy_len[0], "all 0x0C header bytes");

    /* The header left partial == 0x0C, so the data starts inside the head. */
    ASSERT_EQ(data_va, copy_src[1], "the paged payload");
    ASSERT_EQ(buf0_va + 0x0C, copy_dst[1], "resuming at partial");
    ASSERT_EQ(0x04, copy_len[1], "only 4 bytes fit in the head");
    ASSERT_EQ(data_va + 4, copy_src[2], "the source advanced");
    ASSERT_EQ(buf1_va, copy_dst[2], "the next descriptor, from its start");
    ASSERT_EQ(0x08, copy_len[2], "the remaining 8 bytes");

    /* 0x00E18E8C-0x00E18EB4 */
    ASSERT_EQ(0x10, recv_rec.iov.length, "the head was filled, length kept");
    ASSERT_EQ(0x08, iov1->length, "the partly used one is shortened");
    ASSERT_EQ(0x00, iov2->length, "the unused one is emptied");

    out = (const uint8_t *)va_ptr(buf0_va);
    ASSERT_EQ(0x10, out[0], "the header's first byte");
    ASSERT_EQ(0x80, out[0x0C], "the payload follows it in the same buffer");
    out = (const uint8_t *)va_ptr(buf1_va);
    ASSERT_EQ(0x84, out[0], "and continues in the next");

    ASSERT_EQ(1, rtn_pkt_calls, "the packet was handed back");
    ASSERT_EQ(1, rls_cleanup_calls, "and the cleanup handler released");
}

/*
 * 0x00E18E8C `ble': a copy that ends exactly on a descriptor boundary leaves
 * partial at 0 having already advanced the cursor, so NO length is rewritten
 * and every descriptor from the cursor on is zeroed.
 */
static void test_exact_fit_zeroes_the_rest(void)
{
    sock_get_rec.hdr_len = 0x10;
    sock_get_rec.data_len = 0;

    call_receive();

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(1, copy_calls, "one copy");
    ASSERT_EQ(0x10, copy_len[0], "the whole head descriptor");
    ASSERT_EQ(0x10, recv_rec.iov.length, "the head keeps its length");
    ASSERT_EQ(0, iov1->length, "everything after the cursor is zeroed");
    ASSERT_EQ(0, iov2->length, "including the last");
    ASSERT_EQ(0, getva_calls, "no payload, no page fetch");
}

/*
 * With no data at all the cursor never moves: every descriptor, the head
 * included, ends up empty.
 */
static void test_empty_packet_zeroes_everything(void)
{
    sock_get_rec.hdr_len = 0;
    sock_get_rec.data_len = 0;

    call_receive();

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(0, copy_calls, "nothing to copy");
    ASSERT_EQ(0, recv_rec.iov.length, "the head is emptied too");
    ASSERT_EQ(0, iov1->length, "and the rest");
    ASSERT_EQ(0, iov2->length, "and the rest");
}

/*
 * 0x00E18CC0-0x00E18CCC: a descriptor that is exactly used up is left behind
 * with partial reset, so the next call starts at the following descriptor's
 * offset 0.
 */
static void test_copy_spans_several_descriptors(void)
{
    recv_rec.iov.length = 4;
    iov1->length = 4;
    iov2->length = 0x20;
    sock_get_rec.hdr_len = 0x0A;
    sock_get_rec.data_len = 0;

    call_receive();

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(3, copy_calls, "4 + 4 + 2");
    ASSERT_EQ(4, copy_len[0], "the head");
    ASSERT_EQ(buf1_va, copy_dst[1], "the second, from its start");
    ASSERT_EQ(4, copy_len[1], "filled");
    ASSERT_EQ(buf2_va, copy_dst[2], "the third");
    ASSERT_EQ(2, copy_len[2], "the remaining two bytes");
    ASSERT_EQ(4, recv_rec.iov.length, "unchanged");
    ASSERT_EQ(4, iov1->length, "unchanged");
    ASSERT_EQ(2, iov2->length, "shortened to what it received");
}

/* ============================================================================
 * The rest of the routine
 * ============================================================================ */

/* 0x00E18DA4: the MAC source lands at the record's +0x26 / +0x2A. */
static void test_mac_source_is_reported(void)
{
    call_receive();

    ASSERT_EQ(0xAABBCCDDu, recv_rec.mac_src_hi, "record +0x26");
    ASSERT_EQ(0x1234, recv_rec.mac_src_lo, "record +0x2A");
    ASSERT_EQ(TEST_USER_SOCKET, sock_get_sock, "SOCK_$GET saw the user socket");
}

/*
 * 0x00E18D70 "btst.b #0x3,(0xda,A2)" is word bit 11: the addresses of the
 * received packet are handed back and the packet type is zero-extended out
 * of the header's +0x05.
 */
static void test_header_addresses_are_returned(void)
{
    const uint8_t *hdr = (const uint8_t *)va_ptr(hdr_va);

    *(uint16_t *)(chan + XNS_CHAN_OFF_FLAGS) |= XNS_CHAN_FLAG_BUILD_HEADER;

    call_receive();

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(0, memcmp(&recv_rec, hdr + 6, 24),
              "24 bytes from the header's +0x06");
    ASSERT_EQ(hdr[5], recv_rec.packet_type, "the packet type, zero-extended");
}

static void test_header_addresses_not_returned_without_the_flag(void)
{
    call_receive();

    ASSERT_EQ(0, recv_rec.dest_addr.network, "nothing was copied");
    ASSERT_EQ(0, recv_rec.packet_type, "nor the packet type");
}

/* 0x00E18CFE / 0x00E18D12 / 0x00E18D28: three ways to be the wrong channel. */
static void test_bad_channel(void)
{
    channel_num = XNS_MAX_CHANNELS;
    XNS_IDP_$RECEIVE(&channel_num, &recv_rec, &st);
    ASSERT_EQ(status_$xns_bad_channel, st, "index out of range");

    setup();
    *(uint16_t *)(chan + XNS_CHAN_OFF_STATE) = 0;
    call_receive();
    ASSERT_EQ(status_$xns_bad_channel, st, "channel not active");

    setup();
    *(uint16_t *)(chan + XNS_CHAN_OFF_FLAGS) =
        (uint16_t)(4 << XNS_CHAN_FLAG_AS_ID_SHIFT);
    call_receive();
    ASSERT_EQ(status_$xns_bad_channel, st, "another AS_ID owns it");
}

/*
 * 0x00E18D18 "tst.b (0xda,A2)" / `bmi': bit 15 of the flags word marks an OS
 * channel, and the ownership check is skipped for it.
 */
static void test_os_channel_skips_the_as_id_check(void)
{
    *(uint16_t *)(chan + XNS_CHAN_OFF_FLAGS) =
        (uint16_t)(0x8000u | (4 << XNS_CHAN_FLAG_AS_ID_SHIFT));

    call_receive();

    ASSERT_EQ(status_$ok, st, "a foreign AS_ID does not matter");
}

static void test_no_user_socket(void)
{
    *(uint16_t *)(chan + XNS_CHAN_OFF_USER_SOCKET) = XNS_NO_SOCKET;
    call_receive();
    ASSERT_EQ(status_$xns_no_socket, st, "status");
    ASSERT_EQ(0, sock_get_calls, "SOCK_$GET never ran");
}

static void test_no_packet_waiting(void)
{
    sock_get_result = 0;
    call_receive();
    ASSERT_EQ(status_$xns_no_data, st, "status");
    ASSERT_EQ(0, rtn_pkt_calls, "nothing to return");
}

/* 0x00E18DC8: the head descriptor's address must not be zero. */
static void test_null_buffer_address(void)
{
    recv_rec.iov.address = 0;
    call_receive();
    ASSERT_EQ(status_$xns_illegal_buffer_spec, st, "status");
    ASSERT_EQ(1, rtn_pkt_calls, "the packet is still handed back");
}

/* 0x00E18DE0: a negative length is illegal; 0x00E18DE8: so is a positive one
 * with no address. */
static void test_bad_descriptor_in_the_chain(void)
{
    iov1->length = -1;
    call_receive();
    ASSERT_EQ(status_$xns_illegal_buffer_spec, st, "negative length");

    setup();
    iov1->address = 0;
    call_receive();
    ASSERT_EQ(status_$xns_illegal_buffer_spec, st, "length without address");

    /* A zero length with no address is fine (0x00E18DE4 `ble'). */
    setup();
    iov1->length = 0;
    iov1->address = 0;
    iov2->length = 0x20;
    call_receive();
    ASSERT_EQ(status_$ok, st, "an empty descriptor is skipped");
}

/* 0x00E18E1C: hdr_len + data_len must fit in the chain. */
static void test_buffer_too_small(void)
{
    sock_get_rec.hdr_len = 0x20;
    sock_get_rec.data_len = 0x21;       /* 0x41 > 0x30 */

    call_receive();

    ASSERT_EQ(status_$xns_buffer_too_small, st, "status");
    ASSERT_EQ(0, copy_calls, "nothing was copied");
    ASSERT_EQ(1, rtn_pkt_calls, "the packet is handed back");
}

/* 0x00E18E52: a failed page fetch clears the VA cell before the return. */
static void test_getva_failure(void)
{
    getva_status = status_$xns_bad_channel;    /* any non-zero code */

    call_receive();

    ASSERT_EQ(status_$xns_bad_channel, st, "the status is passed through");
    ASSERT_EQ(1, getva_calls, "the fetch was attempted");
    ASSERT_EQ(0x00007000u, getva_handle, "with data_pages[0]");
    ASSERT_EQ(0, rtn_pkt_va, "the VA cell was cleared");
    ASSERT_EQ(0, copy_calls, "nothing was copied");
}

/* 0x00E18DBA / 0x00E18EE4: the cleanup handler firing returns the packet and
 * reports the status FIM_$CLEANUP gave. */
static void test_cleanup_handler_fired(void)
{
    cleanup_result = status_$xns_no_data;      /* any non-"handler set" code */

    call_receive();

    ASSERT_EQ(status_$xns_no_data, st, "the cleanup status is reported");
    ASSERT_EQ(1, rtn_pkt_calls, "the packet was handed back");
    ASSERT_EQ(hdr_va, rtn_pkt_hdr, "with the header VA");
    ASSERT_EQ(0, rls_cleanup_calls, "the handler is not released again");
}

int main(void)
{
    printf("XNS_IDP_$RECEIVE (0x00E18CE2) tests\n");

    RUN_TEST(test_partial_descriptor_is_shortened);
    RUN_TEST(test_exact_fit_zeroes_the_rest);
    RUN_TEST(test_empty_packet_zeroes_everything);
    RUN_TEST(test_copy_spans_several_descriptors);
    RUN_TEST(test_mac_source_is_reported);
    RUN_TEST(test_header_addresses_are_returned);
    RUN_TEST(test_header_addresses_not_returned_without_the_flag);
    RUN_TEST(test_bad_channel);
    RUN_TEST(test_os_channel_skips_the_as_id_check);
    RUN_TEST(test_no_user_socket);
    RUN_TEST(test_no_packet_waiting);
    RUN_TEST(test_null_buffer_address);
    RUN_TEST(test_bad_descriptor_in_the_chain);
    RUN_TEST(test_buffer_too_small);
    RUN_TEST(test_getva_failure);
    RUN_TEST(test_cleanup_handler_fired);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
