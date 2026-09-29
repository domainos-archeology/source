/*
 * ring/test/test_send_os.c - unit tests for RING_$SEND_OS (0x00E77C60)
 *
 * Target memory (header pages) is a host arena reached through
 * ARCH_HOST_VA_BASE; every callee is mocked.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "ring/ring_internal.h"
#include "mac_os/mac_os.h"
#include "node/node.h"
#include "os/os.h"

MODULE_DATA_DEFINE(ring_global_t, RING_$CTL, 0x00E86400);
uint32_t NODE_$ME;

#define ARENA_VA   0x00100000u
static uint8_t arena[0x3000] __attribute__((aligned(0x400)));
#define AT(va)     (arena + ((va) - ARENA_VA))

#define NETBUF_VA  0x00101000u   /* the page NETBUF_$GET_HDR hands out */
#define NETBUF_PA  0x00ABC000u
#define USER_HDR   0x00100100u   /* caller header, not at page + 0x1C */
#define PRE_PAGE   0x00102000u   /* a prebuilt header page */

/* ---- mocks ----------------------------------------------------------- */

static int get_hdr_calls, rtn_hdr_calls, copy_calls, lock_calls, unlock_calls;
static uint32_t rtn_hdr_va[4];
static uint32_t copy_len;
static int sendp_calls;
static uint32_t sendp_hdr_pa, sendp_desc0, sendp_desc1;
static uint16_t sendp_hdr_len, sendp_data_len, sendp_opts, sendp_unit;
static status_$t sendp_status;
static int copy_pkt_calls;
static status_$t copy_pkt_status;
static int demux_calls;
static int16_t demux_port;
static uint32_t demux_hdr_addr, demux_page0;
static status_$t demux_status;
static int dump_calls;

void NETBUF_$GET_HDR(uint32_t *phys_out, uint32_t *va_out)
{
    get_hdr_calls++;
    *phys_out = NETBUF_PA;
    *va_out = NETBUF_VA;
}

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    rtn_hdr_va[rtn_hdr_calls++ & 3] = *va_ptr;
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    copy_calls++;
    copy_len = len;
    memcpy(dst, src, len);
}

void ML_$LOCK(int16_t id) { (void)id; lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlock_calls++; }

void RING_$SENDP(uint16_t *unit_ptr, uint32_t hdr_pa, ring_$pkt_hdr_t *hdr,
                 uint16_t hdr_len, const uint32_t *data_desc,
                 uint32_t unused_1a, uint16_t data_len,
                 const uint16_t *send_opts, uint16_t *result_flags,
                 status_$t *status_ret)
{
    (void)hdr; (void)unused_1a;
    sendp_calls++;
    sendp_unit = *unit_ptr;
    sendp_hdr_pa = hdr_pa;
    sendp_hdr_len = hdr_len;
    sendp_desc0 = data_desc[0];
    sendp_desc1 = data_desc[1];
    sendp_data_len = data_len;
    sendp_opts = *send_opts;
    *result_flags = 0x77;
    *status_ret = sendp_status;
}

void NET_IO_$COPY_PACKET(uint32_t *hdr_src_p, uint16_t hdr_len,
                         uint32_t src_data_va, uint32_t *src_pages,
                         uint16_t data_len, uint32_t *hdr_va_out,
                         uint32_t *data_pages_out, status_$t *status_ret)
{
    (void)hdr_src_p; (void)hdr_len; (void)src_data_va; (void)src_pages;
    (void)data_len;
    copy_pkt_calls++;
    *hdr_va_out = 0x00555000u;
    data_pages_out[0] = 0x00666000u;
    data_pages_out[1] = data_pages_out[2] = data_pages_out[3] = 0;
    *status_ret = copy_pkt_status;
}

void MAC_OS_$DEMUX(mac_os_$rcv_pkt_t *pkt_info, int16_t *port_num,
                   void *param3, status_$t *status_ret)
{
    mac_os_$send_pkt_t *p = (mac_os_$send_pkt_t *)(void *)pkt_info;
    (void)param3;
    demux_calls++;
    demux_port = *port_num;
    demux_hdr_addr = p->hdr_desc.address;
    demux_page0 = p->data_pages[0];
    *status_ret = demux_status;
}

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    (void)buffers; (void)len;
    dump_calls++;
}

#include "../send_os.c"

/* ---- helpers --------------------------------------------------------- */

static mac_os_$send_pkt_t pkt;
static int16_t channel;
static int16_t sent;
static ring_unit_t *u;

static void reset_state(void)
{
    memset(&RING_$CTL, 0, sizeof(RING_$CTL));
    memset(arena, 0, sizeof(arena));
    memset(&pkt, 0, sizeof(pkt));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - (uintptr_t)ARENA_VA;
    NODE_$ME = 0x00012345;
    u = &RING_$CTL.units[1];
    u->state_flags = RING_UNIT_STARTED;
    u->tmask = 1;
    RING_UNIT_CHANNEL(u, 2).flags = (boolean)0xFF;
    RING_$CTL.port_array[1] = 7;
    channel = 2;
    sent = 0x55;
    get_hdr_calls = rtn_hdr_calls = copy_calls = lock_calls = unlock_calls = 0;
    sendp_calls = copy_pkt_calls = demux_calls = dump_calls = 0;
    sendp_status = copy_pkt_status = demux_status = status_$ok;
    /* a packet for node 0x00098765 with an 8-byte caller header */
    pkt.link_addr.n_words = 2;
    pkt.link_addr.addr[0] = 0x0009;
    pkt.link_addr.addr[1] = 0x8765;
    pkt.hdr_desc.length = 8;
    pkt.hdr_desc.address = USER_HDR;
    memcpy(AT(USER_HDR), "ABCDEFGH", 8);
    pkt.frame_type = 0x600;
    pkt.data_length = 0x100;
    pkt.data_pages[0] = 0x00777000u;
}

static status_$t send(void)
{
    status_$t st = 0x99;
    RING_$SEND_OS(1, &channel, &pkt, &sent, &st);
    return st;
}

/* ---- tests ----------------------------------------------------------- */

static void test_offline(void)
{
    u->tmask = 0;
    ASSERT_EQ(status_$ring_device_offline, send());
    ASSERT_EQ(0, sent);                        /* cleared first */
    u->tmask = 1;
    u->state_flags = 0;
    ASSERT_EQ(status_$ring_device_offline, send());
}

static void test_channel_not_open(void)
{
    channel = 3;
    ASSERT_EQ(status_$ring_channel_not_open, send());
    channel = 0;
    ASSERT_EQ(status_$ring_channel_not_open, send());
    channel = 11;
    ASSERT_EQ(status_$ring_channel_not_open, send());
}

static void test_header_too_long(void)
{
    pkt.hdr_desc.length = 0x3C9;
    ASSERT_EQ(status_$ring_illegal_header_length, send());
    ASSERT_EQ(0, get_hdr_calls);
}

static void test_data_too_long_leaks_header(void)
{
    pkt.data_length = 0x401;
    ASSERT_EQ(status_$ring_invalid_data_length, send());
    ASSERT_EQ(1, get_hdr_calls);
    ASSERT_EQ(0, rtn_hdr_calls);               /* the quirk */
}

static void test_bad_dest_address(void)
{
    pkt.link_addr.n_words = 3;
    ASSERT_EQ(status_$ring_illegal_dest_address, send());
    ASSERT_EQ(0, rtn_hdr_calls);
}

static void test_remote_send_builds_header(void)
{
    ring_$pkt_hdr_t *h = (ring_$pkt_hdr_t *)(void *)AT(NETBUF_VA);
    ASSERT_EQ(status_$ok, send());
    ASSERT_EQ(1, copy_calls);
    ASSERT_EQ(8, copy_len);
    ASSERT_EQ(0, memcmp(AT(NETBUF_VA + 0x1C), "ABCDEFGH", 8));
    ASSERT_EQ(0x00098765, h->msg_type);
    ASSERT_EQ(0x01, h->flags);
    ASSERT_EQ(0x00012345, h->src_id);
    ASSERT_EQ(1, h->chksum);
    ASSERT_EQ(0x24, h->hdr_len);
    ASSERT_EQ(0x100, h->data_len);
    ASSERT_EQ(0x600, h->route_info);
    ASSERT_EQ(1, sendp_calls);
    ASSERT_EQ(1, sendp_unit);
    ASSERT_EQ(NETBUF_PA, sendp_hdr_pa);
    ASSERT_EQ(0x24, sendp_hdr_len);
    ASSERT_EQ(0x00777000u, sendp_desc0);
    ASSERT_EQ(NETBUF_VA, sendp_desc1);
    ASSERT_EQ(0x100, sendp_data_len);
    ASSERT_EQ(0, sendp_opts);
    ASSERT_EQ(0x77, sent);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, demux_calls);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(NETBUF_VA, rtn_hdr_va[0]);
}

static void test_send_failure_reported(void)
{
    sendp_status = status_$ring_transmit_failed;
    ASSERT_EQ(status_$ring_transmit_failed, send());
    ASSERT_EQ(1, rtn_hdr_calls);
}

static void test_prebuilt_header_page(void)
{
    ring_$pkt_hdr_t *h = (ring_$pkt_hdr_t *)(void *)AT(PRE_PAGE);
    pkt.hdr_desc.address = PRE_PAGE + 0x1C;
    *(uint32_t *)(void *)AT(PRE_PAGE + 0x3FC) = 0x00DEF000u;
    ASSERT_EQ(status_$ok, send());
    ASSERT_EQ(0, get_hdr_calls);
    ASSERT_EQ(0, copy_calls);
    ASSERT_EQ(0x00DEF000u, sendp_hdr_pa);
    ASSERT_EQ(0x00098765, h->msg_type);
    ASSERT_EQ(0, rtn_hdr_calls);
}

static void test_local_loopback(void)
{
    pkt.link_addr.addr[0] = 0x0001;
    pkt.link_addr.addr[1] = 0x2345;            /* this node */
    ASSERT_EQ(status_$ok, send());
    ASSERT_EQ(0, sendp_calls);
    ASSERT_EQ(1, copy_pkt_calls);
    ASSERT_EQ(1, demux_calls);
    ASSERT_EQ(7, demux_port);
    ASSERT_EQ(0x00555000u + 0x1C, demux_hdr_addr);
    ASSERT_EQ(0x00666000u, demux_page0);
    ASSERT_EQ(0, dump_calls);
    ASSERT_EQ(1, rtn_hdr_calls);               /* only our own header page */
}

static void test_loopback_demux_failure_swallowed(void)
{
    pkt.link_addr.addr[0] = 0x0001;
    pkt.link_addr.addr[1] = 0x2345;
    demux_status = 0x003A0001;
    ASSERT_EQ(status_$ok, send());
    ASSERT_EQ(1, dump_calls);
    ASSERT_EQ(2, rtn_hdr_calls);
    ASSERT_EQ(0x00555000u, rtn_hdr_va[0]);
    ASSERT_EQ(NETBUF_VA, rtn_hdr_va[1]);
}

static void test_loopback_copy_failure(void)
{
    pkt.link_addr.addr[0] = 0x0001;
    pkt.link_addr.addr[1] = 0x2345;
    copy_pkt_status = 0x00110001;
    ASSERT_EQ(status_$ok, send());
    ASSERT_EQ(0, demux_calls);
    ASSERT_EQ(1, rtn_hdr_calls);
}

static void test_broadcast_goes_both_ways(void)
{
    ring_$pkt_hdr_t *h = (ring_$pkt_hdr_t *)(void *)AT(NETBUF_VA);
    pkt.is_broadcast = (int8_t)0xFF;
    sendp_status = status_$ring_transmit_failed;
    ASSERT_EQ(status_$ring_transmit_failed, send());
    ASSERT_EQ(0x81, h->flags);
    ASSERT_EQ(1, sendp_calls);
    ASSERT_EQ(1, demux_calls);
}

int main(void)
{
    printf("RING_$SEND_OS tests:\n");
    RUN_TEST(offline);
    RUN_TEST(channel_not_open);
    RUN_TEST(header_too_long);
    RUN_TEST(data_too_long_leaks_header);
    RUN_TEST(bad_dest_address);
    RUN_TEST(remote_send_builds_header);
    RUN_TEST(send_failure_reported);
    RUN_TEST(prebuilt_header_page);
    RUN_TEST(local_loopback);
    RUN_TEST(loopback_demux_failure_swallowed);
    RUN_TEST(loopback_copy_failure);
    RUN_TEST(broadcast_goes_both_ways);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
