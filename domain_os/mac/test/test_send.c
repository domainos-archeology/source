/*
 * mac/test/test_send.c
 *
 * MAC_$SEND (0x00E0BB12) validates the channel against MAC_OS_$CHANNEL_TABLE,
 * optionally resolves the link address with MAC_OS_$ARP, copies the caller's
 * descriptor into a local one with hdr_prebuilt cleared, clears a flag byte in
 * every entry of the caller's buffer chain, and hands the local descriptor to
 * MAC_OS_$SEND.
 *
 * Bead source-d6vh: the body used to sit under "#if defined(ARCH_M68K)" with a
 * not-implemented stub and reached the channel table through raw address
 * arithmetic on MAC_$DATA_BASE.  These tests exercise the ported body.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-48s ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "mac/mac_internal.h"

/* ------------------------------------------------------------------ */
/* Module data and stubs                                               */

mac_os_$channel_t MAC_OS_$CHANNEL_TABLE[MAC_OS_CHANNEL_TABLE_SLOTS];
uint16_t          PROC1_$AS_ID;

const rip_$nexthop_t MAC_OS_$BROADCAST_NEXTHOP = {
    .network = 0x00000000u,
    .host_hi = 0xFFFFu,
    .host_lo = 0xFFFFFFFFu
};

/* FIM_$CLEANUP / FIM_$RLS_CLEANUP */
static status_$t  cleanup_returns;
static int        cleanup_calls;
static int        rls_calls;
static void      *cleanup_ctx;

status_$t FIM_$CLEANUP(void *ctx)
{
    cleanup_calls++;
    cleanup_ctx = ctx;
    return cleanup_returns;
}

void FIM_$RLS_CLEANUP(void *ctx)
{
    rls_calls++;
    (void)ctx;
}

/* MAC_OS_$ARP */
static int        arp_calls;
static void      *arp_addr_info;
static int16_t    arp_port;
static uint16_t  *arp_mac_addr;
static uint8_t   *arp_flags;
static status_$t  arp_sets_status;

void MAC_OS_$ARP(void *addr_info, int16_t port_num, uint16_t *mac_addr,
                 uint8_t *flags, status_$t *status_ret)
{
    arp_calls++;
    arp_addr_info = addr_info;
    arp_port      = port_num;
    arp_mac_addr  = mac_addr;
    arp_flags     = flags;
    *status_ret   = arp_sets_status;
}

/* MAC_OS_$SEND */
static int              os_send_calls;
static int16_t         *os_send_channel;
static mac_$send_pkt_t  os_send_pkt;
static int16_t         *os_send_bytes_cell;
static int16_t          os_send_sets_bytes;
static status_$t        os_send_sets_status;

void MAC_OS_$SEND(int16_t *channel, mac_os_$send_pkt_t *pkt_desc,
                  int16_t *bytes_sent, status_$t *status_ret)
{
    os_send_calls++;
    os_send_channel    = channel;
    os_send_pkt        = *pkt_desc;
    os_send_bytes_cell = bytes_sent;
    *bytes_sent        = os_send_sets_bytes;
    *status_ret        = os_send_sets_status;
}

#include "../send.c"

/* ------------------------------------------------------------------ */
/* Fixture                                                             */

/*
 * The caller's chain entry: a mac_os_$buf_desc_t with the flag byte the image
 * clears sitting immediately after it (0x00E0BBFA "clr.b (0xc,A0)").
 */
typedef struct chain_entry_t {
    mac_os_$buf_desc_t desc;        /* 0x00..0x0B */
    uint8_t            flag;        /* 0x0C */
    uint8_t            pad[3];
} chain_entry_t;

static mac_$send_pkt_t caller_pkt;
static chain_entry_t   chain[3];
static uint16_t        channel_num;
static uint16_t        bytes_sent;
static status_$t       status;

static void reset_all(void)
{
    memset(MAC_OS_$CHANNEL_TABLE, 0, sizeof(MAC_OS_$CHANNEL_TABLE));
    memset(&caller_pkt, 0, sizeof(caller_pkt));
    memset(chain, 0, sizeof(chain));
    memset(&os_send_pkt, 0, sizeof(os_send_pkt));

    /* mac_os_$buf_desc_t.next is a 32-bit target VA. */
    ARCH_HOST_VA_BASE = (uintptr_t)&chain[0] - 0x1000u;

    cleanup_returns = status_$cleanup_handler_set;
    cleanup_calls   = 0;
    rls_calls       = 0;

    arp_calls       = 0;
    arp_sets_status = status_$ok;

    os_send_calls       = 0;
    os_send_sets_bytes  = 0x1234;
    os_send_sets_status = status_$ok;

    PROC1_$AS_ID = 5;

    /* Channel 2: in use, owned by AS 5, on port 6. */
    MAC_OS_$CHANNEL_TABLE[2].flags =
        (uint16_t)(MAC_OS_CHANNEL_IN_USE | (5u << MAC_OS_CHANNEL_OWNER_SHIFT));
    MAC_OS_$CHANNEL_TABLE[2].port_index = 6;

    channel_num = 2;
    bytes_sent  = 0xEEEE;
    status      = 0x5A5A5A5A;
}

/* ------------------------------------------------------------------ */

/* 0x00E0BB20-0x00E0BB2A: both out-cells are cleared before any check. */
TEST(out_cells_are_cleared_before_the_channel_check)
{
    reset_all();
    channel_num = 99;               /* rejected at 0x00E0BB30 */

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(0u, bytes_sent);
    ASSERT_EQ(status_$mac_channel_not_open, status);
    ASSERT_EQ(0, cleanup_calls);
}

/* 0x00E0BB30 cmpi.w #0xa / bcc: ten channels, so 10 is out of range. */
TEST(channel_ten_is_out_of_range)
{
    reset_all();
    channel_num = MAC_MAX_CHANNELS;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(status_$mac_channel_not_open, status);
    ASSERT_EQ(0, os_send_calls);
}

/* 0x00E0BB48 btst.l #0x9 on the flags WORD. */
TEST(a_channel_not_in_use_is_refused)
{
    reset_all();
    MAC_OS_$CHANNEL_TABLE[2].flags &= (uint16_t)~MAC_OS_CHANNEL_IN_USE;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(status_$mac_channel_not_open, status);
    ASSERT_EQ(0, cleanup_calls);
}

/*
 * 0x00E0BB52-0x00E0BB62: "and.b" on the flags word's HIGH byte then
 * "lsr.w #2" - the owner is word bits 10..15, compared against PROC1_$AS_ID.
 */
TEST(the_owner_is_word_bits_10_through_15)
{
    reset_all();
    PROC1_$AS_ID = 6;               /* the channel says 5 */

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(status_$mac_channel_not_open, status);
    ASSERT_EQ(0, cleanup_calls);

    reset_all();
    MAC_OS_$CHANNEL_TABLE[2].flags =
        (uint16_t)(MAC_OS_CHANNEL_IN_USE | (0x3Fu << MAC_OS_CHANNEL_OWNER_SHIFT));
    PROC1_$AS_ID = 0x3F;            /* the widest owner the field holds */

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, os_send_calls);
}

/*
 * 0x00E0BB6E-0x00E0BB84: a cleanup status other than
 * status_$cleanup_handler_set is the fault unwind - it becomes the caller's
 * status and the handler is NOT released.
 */
TEST(the_cleanup_unwind_returns_the_fault_status)
{
    reset_all();
    cleanup_returns = 0x00120003;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(0x00120003, status);
    ASSERT_EQ(0, rls_calls);
    ASSERT_EQ(0, os_send_calls);
}

/*
 * 0x00E0BB8C tst.b (0x18,A0) / bpl: a false is_broadcast skips ARP entirely.
 */
TEST(arp_is_skipped_when_is_broadcast_is_false)
{
    reset_all();
    caller_pkt.is_broadcast = 0;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(0, arp_calls);
    ASSERT_EQ(1, os_send_calls);
}

/*
 * 0x00E0BB92-0x00E0BBAE: the arguments are the module's constant broadcast
 * next hop, the channel's port index, the caller's descriptor as the link
 * address output, a local byte and the caller's status cell.
 */
TEST(arp_gets_the_broadcast_nexthop_and_the_channels_port)
{
    reset_all();
    caller_pkt.is_broadcast = -1;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(1, arp_calls);
    ASSERT_TRUE(arp_addr_info == (void *)&MAC_OS_$BROADCAST_NEXTHOP);
    ASSERT_EQ(6, arp_port);
    ASSERT_TRUE(arp_mac_addr == (uint16_t *)&caller_pkt);
    ASSERT_TRUE(arp_flags != (uint8_t *)&caller_pkt.is_broadcast);
    ASSERT_EQ(1, os_send_calls);
}

/* The image bytes at 0x00E23270. */
TEST(the_broadcast_nexthop_is_the_image_record)
{
    ASSERT_EQ(0x00000000u, MAC_OS_$BROADCAST_NEXTHOP.network);
    ASSERT_EQ(0x0000FFFFu, MAC_OS_$BROADCAST_NEXTHOP.host_hi);
    ASSERT_EQ(0xFFFFFFFFu, MAC_OS_$BROADCAST_NEXTHOP.host_lo);
    ASSERT_EQ(10u, sizeof(rip_$nexthop_t));
}

/* 0x00E0BBB2-0x00E0BBB8: a failed ARP releases the handler and stops. */
TEST(a_failed_arp_releases_the_handler_and_stops)
{
    reset_all();
    caller_pkt.is_broadcast = -1;
    arp_sets_status = 0x003A0009;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(0x003A0009, status);
    ASSERT_EQ(1, rls_calls);
    ASSERT_EQ(0, os_send_calls);
    ASSERT_EQ(0u, bytes_sent);
}

/*
 * 0x00E0BBBA-0x00E0BBF2: exactly which fields of the local descriptor come
 * from the caller's, and which two are forced.
 */
TEST(the_local_descriptor_is_built_field_by_field)
{
    reset_all();

    caller_pkt.link_addr.n_words = 3;
    caller_pkt.link_addr.addr[0] = 0x1111;
    caller_pkt.link_addr.addr[10] = 0x2222;     /* the last word of 0x00..0x17 */
    caller_pkt.is_broadcast = 0;
    caller_pkt.frame_type   = 0x600;
    caller_pkt.hdr_prebuilt = -1;               /* MUST be cleared */
    caller_pkt.data_length  = 0xDEADBEEF;       /* MUST be cleared */
    caller_pkt.data_pages[0] = 0xCAFEBABE;      /* MUST be cleared */
    caller_pkt.data_pages[1] = 0x99999999;      /* NOT copied, NOT cleared */
    caller_pkt.hdr_desc.length  = 0x40;
    caller_pkt.hdr_desc.address = 0x11223344;
    caller_pkt.hdr_desc.next    = 0;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(1, os_send_calls);
    ASSERT_EQ(3u,      os_send_pkt.link_addr.n_words);
    ASSERT_EQ(0x1111u, os_send_pkt.link_addr.addr[0]);
    ASSERT_EQ(0x2222u, os_send_pkt.link_addr.addr[10]);
    ASSERT_EQ(0,       os_send_pkt.is_broadcast);
    ASSERT_EQ(0x600u,  os_send_pkt.frame_type);
    ASSERT_EQ(0,       os_send_pkt.hdr_prebuilt);
    ASSERT_EQ(0u,      os_send_pkt.data_length);
    ASSERT_EQ(0u,      os_send_pkt.data_pages[0]);
    ASSERT_EQ(0x40,       os_send_pkt.hdr_desc.length);
    ASSERT_EQ(0x11223344u, os_send_pkt.hdr_desc.address);

    /* The caller's own record is untouched. */
    ASSERT_EQ(-1,          caller_pkt.hdr_prebuilt);
    ASSERT_EQ(0xDEADBEEFu, caller_pkt.data_length);
}

/*
 * 0x00E0BBF4-0x00E0BC06: the walk starts at the CALLER's hdr_desc.next and
 * clears the byte one past each 12-byte descriptor.
 */
TEST(every_chain_entry_gets_its_flag_byte_cleared)
{
    reset_all();

    chain[0].flag = 0xAA;
    chain[1].flag = 0xBB;
    chain[2].flag = 0xCC;
    chain[0].desc.next = ARCH_PTR_TO_VA(&chain[1]);
    chain[1].desc.next = ARCH_PTR_TO_VA(&chain[2]);
    chain[2].desc.next = 0;
    caller_pkt.hdr_desc.next = ARCH_PTR_TO_VA(&chain[0]);

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(0u, chain[0].flag);
    ASSERT_EQ(0u, chain[1].flag);
    ASSERT_EQ(0u, chain[2].flag);
    ASSERT_EQ(1, os_send_calls);
}

/* A null chain head leaves the loop unentered (0x00E0BC02 cmpa.w #0x0). */
TEST(a_null_chain_head_walks_nothing)
{
    reset_all();
    chain[0].flag = 0xAA;
    caller_pkt.hdr_desc.next = 0;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_EQ(0xAAu, chain[0].flag);
    ASSERT_EQ(1, os_send_calls);
}

/*
 * 0x00E0BC08-0x00E0BC32: the caller's channel cell is forwarded unchanged, the
 * word and longword results are copied back, and the handler is released.
 */
TEST(results_come_back_from_mac_os_send)
{
    reset_all();
    os_send_sets_bytes  = 0x0140;
    os_send_sets_status = 0x00A50001;

    MAC_$SEND(&channel_num, &caller_pkt, &bytes_sent, &status);

    ASSERT_TRUE(os_send_channel == (int16_t *)&channel_num);
    ASSERT_TRUE(os_send_bytes_cell != (int16_t *)&bytes_sent);
    ASSERT_EQ(0x0140u,     bytes_sent);
    ASSERT_EQ(0x00A50001,  status);
    ASSERT_EQ(1, rls_calls);
}

int main(void)
{
    printf("MAC_$SEND tests\n");
    RUN_TEST(out_cells_are_cleared_before_the_channel_check);
    RUN_TEST(channel_ten_is_out_of_range);
    RUN_TEST(a_channel_not_in_use_is_refused);
    RUN_TEST(the_owner_is_word_bits_10_through_15);
    RUN_TEST(the_cleanup_unwind_returns_the_fault_status);
    RUN_TEST(arp_is_skipped_when_is_broadcast_is_false);
    RUN_TEST(arp_gets_the_broadcast_nexthop_and_the_channels_port);
    RUN_TEST(the_broadcast_nexthop_is_the_image_record);
    RUN_TEST(a_failed_arp_releases_the_handler_and_stops);
    RUN_TEST(the_local_descriptor_is_built_field_by_field);
    RUN_TEST(every_chain_entry_gets_its_flag_byte_cleared);
    RUN_TEST(a_null_chain_head_walks_nothing);
    RUN_TEST(results_come_back_from_mac_os_send);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
