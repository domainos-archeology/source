/*
 * msg/test/test_send.c - Unit tests for MSG_$$SEND, MSG_$SENDI and MSG_$SEND
 * (0x00E0D9EC / 0x00E59AA6 / 0x00E599FC).
 *
 * The test compiles the real msg/send.c and supplies scripted versions of
 * everything it calls, so the local-delivery path, the bounce-page path, the
 * netbuf-copy path, the explicit-port override and the cleanup (including the
 * register-aliasing defect in the original) can each be driven from C.
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

#include "msg/msg_internal.h"

MODULE_DATA_DEFINE(msg_$unwired_data_t, MSG_$UNWIRED_DATA, 0x00E80D84);
MODULE_DATA_DEFINE(msg_$wired_data_t, MSG_$WIRED_DATA, 0x00E242E4);

uint32_t NODE_$ME;
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];

#define TEST_PORT       3
#define TEST_HDR_VA     0x00000800u
#define TEST_HDR_PA     0x0004AC00u
#define TEST_DPAGE_VA   0x00001800u
#define TEST_DPAGE_PA   0x0004B000u
#define TEST_NODE_ME    0x000ABCDEu
#define TEST_REMOTE     0x00012345u

/*
 * Target virtual addresses are 32 bits wide, so everything the code under
 * test resolves with ARCH_VA_TO_PTR lives in this arena.
 */
static uint8_t va_arena[0x4000];
#define TEST_DRV_VA     0x00000100u

/* --- NETBUF_$GET_HDR / NETBUF_$RTN_HDR ------------------------------------ */
static int get_hdr_calls;
static int rtn_hdr_calls;
static uint32_t rtn_hdr_va_seen;

void NETBUF_$GET_HDR(uint32_t *phys_out, uint32_t *va_out)
{
    get_hdr_calls++;
    *phys_out = TEST_HDR_PA;
    *va_out = TEST_HDR_VA;
}

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    rtn_hdr_calls++;
    rtn_hdr_va_seen = *va_ptr;
}

/* --- PKT_$BLD_INTERNET_HDR ------------------------------------------------ */
struct bld_args {
    uint32_t routing_key;
    uint32_t dest_node;
    uint16_t dest_sock;
    int32_t src_node_or;
    uint32_t src_node;
    uint16_t src_sock;
    uint16_t info_flags;
    uint16_t request_id;
    void *template;
    uint16_t template_len;
    uint16_t data_len;
    pkt_$hdr_t *hdr;
};
static struct bld_args last_bld;
static int bld_calls;
static status_$t bld_status;
static int16_t bld_port_out;
static uint16_t bld_len_out;

void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node,
                           uint16_t dest_sock, int32_t src_node_or,
                           uint32_t src_node, uint16_t src_sock,
                           const pkt_$info_t *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len,
                           uint16_t data_len, int16_t *port_out,
                           pkt_$hdr_t *hdr, uint16_t *len_out,
                           uint16_t *retry_hint, uint16_t *timeout_out,
                           status_$t *status_ret)
{
    bld_calls++;
    last_bld.routing_key = routing_key;
    last_bld.dest_node = dest_node;
    last_bld.dest_sock = dest_sock;
    last_bld.src_node_or = src_node_or;
    last_bld.src_node = src_node;
    last_bld.src_sock = src_sock;
    last_bld.info_flags = pkt_info->flags;
    last_bld.request_id = request_id;
    last_bld.template = template;
    last_bld.template_len = template_len;
    last_bld.data_len = data_len;
    last_bld.hdr = hdr;

    *port_out = bld_port_out;
    *len_out = bld_len_out;
    *retry_hint = 5;
    *timeout_out = 4;
    *status_ret = bld_status;
}

/* --- PKT_$COPY_TO_PA / PKT_$DUMP_DATA ------------------------------------- */
static int copy_to_pa_calls;
static uint16_t copy_to_pa_len;
static status_$t copy_to_pa_status;
static uint32_t copy_to_pa_page;
static int dump_data_calls;
static int16_t dump_data_len;
static uint32_t dump_data_page0;

void PKT_$COPY_TO_PA(char *src_va, uint16_t len, uint32_t *buffers_out,
                     status_$t *status_ret)
{
    (void)src_va;
    copy_to_pa_calls++;
    copy_to_pa_len = len;
    buffers_out[0] = copy_to_pa_page;
    *status_ret = copy_to_pa_status;
}

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    dump_data_calls++;
    dump_data_len = len;
    dump_data_page0 = buffers[0];
}

/* --- OS_$DATA_COPY -------------------------------------------------------- */
static int os_copy_calls;
static void *os_copy_dst;
static uint32_t os_copy_len;

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    os_copy_calls++;
    os_copy_dst = dst;
    os_copy_len = len;
    memcpy(dst, src, (size_t)len);
}

/* --- TIME_$ABS_CLOCK ------------------------------------------------------ */
void TIME_$ABS_CLOCK(clock_t *clock)
{
    clock->high = 0x11223344u;
    clock->low = 0x5566;
}

/* --- SOCK_$PUT ------------------------------------------------------------ */
static int sock_put_calls;
static uint16_t sock_put_sock;
static sock_$pkt_info_t sock_put_rec;
static int8_t sock_put_flags;
static uint16_t sock_put_ec1;
static uint16_t sock_put_ec2;
static int8_t sock_put_result;

int8_t SOCK_$PUT(uint16_t sock_num, sock_$pkt_info_t *pkt_info, int8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2)
{
    sock_put_calls++;
    sock_put_sock = sock_num;
    sock_put_rec = *pkt_info;
    sock_put_flags = flags;
    sock_put_ec1 = ec_param1;
    sock_put_ec2 = ec_param2;
    return sock_put_result;
}

/* --- ML_$LOCK / ML_$UNLOCK ------------------------------------------------ */
static int lock_calls;
static int unlock_calls;
static int16_t lock_id_seen;

void ML_$LOCK(int16_t resource_id) { lock_calls++; lock_id_seen = resource_id; }
void ML_$UNLOCK(int16_t resource_id) { unlock_calls++; (void)resource_id; }

/* --- NET_IO_$SEND --------------------------------------------------------- */
struct net_send_args {
    int16_t port;
    uint32_t hdr_va;
    uint32_t hdr_pa;
    uint16_t hdr_len;
    uint32_t data_va;
    uint32_t data_page0;
    int16_t data_len;
    uint16_t flags;
};
static struct net_send_args last_net_send;
static int net_send_calls;
static status_$t net_send_status;
static net_io_$send_info_t net_send_info_out;

void NET_IO_$SEND(int16_t port, uint32_t *hdr_ptr, uint32_t hdr_pa,
                  uint16_t hdr_len, uint32_t data_va, uint32_t *data_pages,
                  int16_t data_len, uint16_t flags,
                  net_io_$send_info_t *send_info, status_$t *status_ret)
{
    net_send_calls++;
    last_net_send.port = port;
    last_net_send.hdr_va = *hdr_ptr;
    last_net_send.hdr_pa = hdr_pa;
    last_net_send.hdr_len = hdr_len;
    last_net_send.data_va = data_va;
    last_net_send.data_page0 = data_pages[0];
    last_net_send.data_len = data_len;
    last_net_send.flags = flags;
    *send_info = net_send_info_out;
    *status_ret = net_send_status;
}

#include "../send.c"

/* ==========================================================================
 * Fixture
 * ========================================================================== */

static route_$port_t test_port;
static uint8_t template_buf[16];
static uint8_t payload_buf[0x40];

static net_io_$send_info_t out_info;
static status_$t out_status;

static void reset_state(void)
{
    int i;

    memset(va_arena, 0, sizeof(va_arena));
    ARCH_HOST_VA_BASE = (uintptr_t)va_arena;

    memset(&MSG_$UNWIRED_DATA, 0, sizeof(MSG_$UNWIRED_DATA));
    memset(&MSG_$WIRED_DATA, 0, sizeof(MSG_$WIRED_DATA));
    memset(&test_port, 0, sizeof(test_port));
    memset(ROUTE_$WIRED_DATA.portp, 0, sizeof(ROUTE_$WIRED_DATA.portp));
    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    memset(&last_bld, 0, sizeof(last_bld));
    memset(&last_net_send, 0, sizeof(last_net_send));
    memset(&sock_put_rec, 0, sizeof(sock_put_rec));

    for (i = 0; i < (int)sizeof(template_buf); i++) {
        template_buf[i] = (uint8_t)(0x10 + i);
    }
    for (i = 0; i < (int)sizeof(payload_buf); i++) {
        payload_buf[i] = (uint8_t)(0x80 + i);
    }

    NODE_$ME = TEST_NODE_ME;

    test_port.port_type = 0x0077;
    test_port.driver_info = TEST_DRV_VA;
    ((route_$driver_info_t *)(va_arena + TEST_DRV_VA))->max_data_len = 0x600;
    ROUTE_$WIRED_DATA.portp[TEST_PORT] = &test_port;
    ROUTE_$PORT_ARRAY[0].port_type = 0xAAAA;
    ROUTE_$PORT_ARRAY[0].socket = 0xBBBB;

    MSG_$WIRED_DATA.dpage_va = TEST_DPAGE_VA;
    MSG_$WIRED_DATA.dpage_pa = TEST_DPAGE_PA;
    MSG_$WIRED_DATA.dpage_lock = -1;      /* free: the pre-increment lands on 0 */

    get_hdr_calls = 0;
    rtn_hdr_calls = 0;
    rtn_hdr_va_seen = 0;

    bld_calls = 0;
    bld_status = status_$ok;
    bld_port_out = TEST_PORT;
    bld_len_out = 0x30;

    copy_to_pa_calls = 0;
    copy_to_pa_len = 0;
    copy_to_pa_status = status_$ok;
    copy_to_pa_page = 0x0004C000u;

    dump_data_calls = 0;
    dump_data_len = 0;
    dump_data_page0 = 0;

    os_copy_calls = 0;
    os_copy_dst = NULL;
    os_copy_len = 0;

    sock_put_calls = 0;
    sock_put_result = -1;               /* Domain true: the socket took it */

    lock_calls = 0;
    unlock_calls = 0;
    lock_id_seen = 0;

    net_send_calls = 0;
    net_send_status = status_$ok;
    net_send_info_out.port_net = 0x1111;
    net_send_info_out.xmit_status = 0x2222;

    memset(&out_info, 0, sizeof(out_info));
    out_status = 0x5A5A5A5Au;
}

static pkt_$info_t caller_info;

static void call_send(int16_t port_num, uint32_t dest_node, uint16_t data_len)
{
    memset(&caller_info, 0, sizeof(caller_info));
    caller_info.flags = 0x0001;
    caller_info.routing_type = PKT_ROUTING_INET;

    MSG_$$SEND(port_num, 0x11223344u, dest_node, 0x0021, -1, TEST_NODE_ME,
               0x0042, &caller_info, 0x00A5, template_buf, 8,
               payload_buf, data_len, &out_info, &out_status);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E0DA16: the template cap is checked before anything is allocated. */
TEST(template_too_long_allocates_nothing)
{
    reset_state();
    memset(&caller_info, 0, sizeof(caller_info));
    MSG_$$SEND(-1, 0, TEST_REMOTE, 0, -1, TEST_NODE_ME, 0, &caller_info, 0,
               template_buf, 0x201, payload_buf, 0, &out_info, &out_status);

    ASSERT_EQ(status_$network_msg_header_too_big, out_status);
    ASSERT_EQ(0, get_hdr_calls);
    ASSERT_EQ(0, bld_calls);
    ASSERT_EQ(0, rtn_hdr_calls);

    /* 0x200 itself is allowed */
    reset_state();
    memset(&caller_info, 0, sizeof(caller_info));
    MSG_$$SEND(-1, 0, TEST_REMOTE, 0, -1, TEST_NODE_ME, 0, &caller_info, 0,
               template_buf, 0x200, payload_buf, 0, &out_info, &out_status);
    ASSERT_EQ(1, get_hdr_calls);
}

/*
 * 0x00E0DA26 - 0x00E0DA38: the caller's packet-info record is copied and bit
 * 2 of the flags word's low byte is forced on before the header is built.
 */
TEST(packet_info_is_copied_with_the_msg_flag)
{
    reset_state();
    call_send(-1, TEST_REMOTE, 0);

    ASSERT_EQ(1, bld_calls);
    ASSERT_EQ(0x0001 | MSG_INFO_FLAG_MSG, last_bld.info_flags);
    ASSERT_EQ(0x0001, caller_info.flags);       /* the caller's copy is intact */
}

/* Every PKT_$BLD_INTERNET_HDR argument, in order (0x00E0DA56 - 0x00E0DA98). */
TEST(header_builder_arguments)
{
    reset_state();
    call_send(-1, TEST_REMOTE, 0x20);

    ASSERT_EQ(0x11223344u, last_bld.routing_key);
    ASSERT_EQ(TEST_REMOTE, last_bld.dest_node);
    ASSERT_EQ(0x0021, last_bld.dest_sock);
    ASSERT_EQ(-1, last_bld.src_node_or);
    ASSERT_EQ(TEST_NODE_ME, last_bld.src_node);
    ASSERT_EQ(0x0042, last_bld.src_sock);
    ASSERT_EQ(0x00A5, last_bld.request_id);
    ASSERT_EQ((long long)(uintptr_t)template_buf,
              (long long)(uintptr_t)last_bld.template);
    ASSERT_EQ(8, last_bld.template_len);
    ASSERT_EQ(0x20, last_bld.data_len);
    ASSERT_EQ((long long)(uintptr_t)ARCH_VA_TO_PTR(TEST_HDR_VA),
              (long long)(uintptr_t)last_bld.hdr);
}

/*
 * A build failure releases the header and reports the builder's status
 * (0x00E0DB02 -> 0x00E0DC96).
 */
TEST(builder_failure_releases_the_header)
{
    reset_state();
    bld_status = 0x00110099;
    call_send(TEST_PORT, TEST_REMOTE, 0);

    ASSERT_EQ(0x00110099, out_status);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(TEST_HDR_VA, rtn_hdr_va_seen);
    ASSERT_EQ(0, net_send_calls);
    ASSERT_EQ(0, sock_put_calls);
}

/*
 * The register-aliasing defect at 0x00E0DCA2: on the early-error path D3
 * still holds the port argument, so port -1 (D3b == 0xFF) makes the cleanup
 * drop MSG_$WIRED_DATA.dpage_lock even though it was never claimed.  Reproduced, not
 * repaired.
 */
TEST(builder_failure_with_port_minus_one_drops_the_dpage_count)
{
    reset_state();
    bld_status = 0x00110099;
    call_send(-1, TEST_REMOTE, 0);
    ASSERT_EQ(-2, MSG_$WIRED_DATA.dpage_lock);          /* started at -1 */
    ASSERT_EQ(0, dump_data_calls);

    /* an even port number has a zero low byte, so the count is left alone */
    reset_state();
    bld_status = 0x00110099;
    call_send(0x0100, TEST_REMOTE, 0);
    ASSERT_EQ(-1, MSG_$WIRED_DATA.dpage_lock);
    ASSERT_EQ(1, dump_data_calls);
}

/*
 * An explicit port overrides the builder's and forgives its "unknown
 * network", then re-runs the two size checks itself (0x00E0DAA4 onwards).
 */
TEST(explicit_port_overrides_and_forgives_unknown_network)
{
    reset_state();
    bld_status = status_$network_unknown_network;
    bld_port_out = 7;
    call_send(TEST_PORT, TEST_REMOTE, 0x20);

    ASSERT_EQ(status_$ok, out_status);
    ASSERT_EQ(TEST_PORT, last_net_send.port);

    /* the driver cap applies again */
    reset_state();
    bld_status = status_$network_unknown_network;
    call_send(TEST_PORT, TEST_REMOTE, 0x601);
    ASSERT_EQ(status_$network_data_length_too_large, out_status);

    /* header + data may run 0x100 past it */
    reset_state();
    bld_status = status_$network_unknown_network;
    bld_len_out = 0x100;
    call_send(TEST_PORT, TEST_REMOTE, 0x600);
    ASSERT_EQ(status_$ok, out_status);

    reset_state();
    bld_status = status_$network_unknown_network;
    bld_len_out = 0x101;
    call_send(TEST_PORT, TEST_REMOTE, 0x600);
    ASSERT_EQ(status_$network_msg_exceeds_max_size, out_status);
}

/* Port -1 keeps the builder's port and does NOT forgive the status. */
TEST(auto_port_keeps_the_builders_choice)
{
    reset_state();
    bld_port_out = 5;
    ROUTE_$WIRED_DATA.portp[5] = &test_port;
    call_send(-1, TEST_REMOTE, 0);
    ASSERT_EQ(5, last_net_send.port);

    reset_state();
    bld_status = status_$network_unknown_network;
    call_send(-1, TEST_REMOTE, 0);
    ASSERT_EQ(status_$network_unknown_network, out_status);
    ASSERT_EQ(0, net_send_calls);
}

/* 0x00E0DB1C: the send-info record starts as {port_type, 0}. */
TEST(send_info_starts_from_the_port)
{
    reset_state();
    sock_put_result = 0;                /* fail the put so the record survives */
    call_send(-1, TEST_NODE_ME, 0);
    ASSERT_EQ(0x0077, out_info.port_net);
}

/*
 * Local delivery: the sock_$pkt_info_t handed to SOCK_$PUT, and the two
 * status bits MSG writes into the caller's record.
 */
TEST(local_delivery_builds_the_socket_record)
{
    reset_state();
    call_send(-1, TEST_NODE_ME, 0);

    ASSERT_EQ(0, copy_to_pa_calls);             /* no payload, no copy */
    ASSERT_EQ(status_$ok, out_status);
    ASSERT_EQ(1, sock_put_calls);
    ASSERT_EQ(0x0021, sock_put_sock);
    /* sock_$pkt_info_t.hdr is the target VA, not a host pointer */
    ASSERT_EQ(TEST_HDR_VA, sock_put_rec.hdr);
    ASSERT_EQ(0x30, sock_put_rec.hdr_len);
    ASSERT_EQ(0, sock_put_rec.data_len);
    ASSERT_EQ(0, sock_put_rec.flags);
    ASSERT_EQ(0, sock_put_rec.n_hops);
    /* the 6-byte clock lands on the src_addr / src_port pair */
    ASSERT_EQ(0x11223344u, sock_put_rec.src_addr);
    ASSERT_EQ(0x5566, sock_put_rec.src_port);
    /* the boolean and the two words from ROUTE_$PORT_ARRAY[0] */
    ASSERT_EQ(-1, sock_put_flags);
    ASSERT_EQ(0xAAAA, sock_put_ec1);
    ASSERT_EQ(0xBBBB, sock_put_ec2);

    ASSERT_EQ(MSG_XMIT_LOCAL | MSG_XMIT_QUEUED, out_info.xmit_status);
    /* the socket now owns the buffer, so nothing is released */
    ASSERT_EQ(0, rtn_hdr_calls);
    ASSERT_EQ(0, dump_data_calls);
}

/* A refused put releases the header and the payload pages (0x00E0DBC2). */
TEST(local_delivery_refused)
{
    reset_state();
    sock_put_result = 0;
    call_send(-1, TEST_NODE_ME, 0x20);

    ASSERT_EQ(1, copy_to_pa_calls);
    ASSERT_EQ(0x20, copy_to_pa_len);
    ASSERT_EQ(MSG_XMIT_LOCAL, out_info.xmit_status);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(1, dump_data_calls);
    ASSERT_EQ(0x20, dump_data_len);
    ASSERT_EQ(0x0004C000u, dump_data_page0);
}

/*
 * On the local path PKT_$COPY_TO_PA reports into the CALLER's status_ret
 * (0x00E0DB34 "pea (A4)"), and a failure skips the put but still sets the
 * local bit.
 */
TEST(local_delivery_copy_failure)
{
    reset_state();
    copy_to_pa_status = 0x00110042;
    call_send(-1, TEST_NODE_ME, 0x20);

    ASSERT_EQ(0x00110042, out_status);
    ASSERT_EQ(0, sock_put_calls);
    ASSERT_EQ(MSG_XMIT_LOCAL, out_info.xmit_status);
    ASSERT_EQ(1, rtn_hdr_calls);
}

/* Remote, short payload: the bounce page is claimed and copied into. */
TEST(remote_uses_the_bounce_page)
{
    reset_state();
    call_send(-1, TEST_REMOTE, 0x20);

    /*
     * The claim is made and dropped inside the call, so the counter is back
     * where it started; os_copy_calls is what proves the page was used.
     */
    ASSERT_EQ(-1, MSG_$WIRED_DATA.dpage_lock);
    ASSERT_EQ(1, os_copy_calls);
    ASSERT_EQ(0x20, os_copy_len);
    ASSERT_EQ((long long)(uintptr_t)ARCH_VA_TO_PTR(TEST_DPAGE_VA),
              (long long)(uintptr_t)os_copy_dst);
    ASSERT_EQ(0, copy_to_pa_calls);

    ASSERT_EQ(TEST_DPAGE_VA, last_net_send.data_va);
    ASSERT_EQ(TEST_DPAGE_PA, last_net_send.data_page0);

    /* the bounce-page path releases the count instead of the netbuf pages */
    ASSERT_EQ(0, dump_data_calls);              /* 0x00E0DCA6 */
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(status_$ok, out_status);
    ASSERT_EQ(0x1111, out_info.port_net);
    ASSERT_EQ(0x2222, out_info.xmit_status);
}

/* Over 0x400 bytes, or a busy page, falls back to netbuf pages. */
TEST(remote_falls_back_to_netbuf_pages)
{
    reset_state();
    call_send(-1, TEST_REMOTE, 0x401);
    ASSERT_EQ(-1, MSG_$WIRED_DATA.dpage_lock);          /* incremented then decremented */
    ASSERT_EQ(0, os_copy_calls);
    ASSERT_EQ(1, copy_to_pa_calls);
    ASSERT_EQ(0, last_net_send.data_va);
    ASSERT_EQ(0x0004C000u, last_net_send.data_page0);
    ASSERT_EQ(1, dump_data_calls);

    /* exactly 0x400 still fits the page */
    reset_state();
    call_send(-1, TEST_REMOTE, 0x400);
    ASSERT_EQ(1, os_copy_calls);

    /* someone else holds the page */
    reset_state();
    MSG_$WIRED_DATA.dpage_lock = 0;
    call_send(-1, TEST_REMOTE, 0x20);
    ASSERT_EQ(0, MSG_$WIRED_DATA.dpage_lock);
    ASSERT_EQ(0, os_copy_calls);
    ASSERT_EQ(1, copy_to_pa_calls);
}

/* A netbuf copy failure aborts before the lock is taken (0x00E0DC40). */
TEST(remote_copy_failure_skips_the_send)
{
    reset_state();
    copy_to_pa_status = 0x00110042;
    call_send(-1, TEST_REMOTE, 0x401);

    ASSERT_EQ(0x00110042, out_status);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, net_send_calls);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(1, dump_data_calls);
}

/* Zero-length payload: no page, no copy (0x00E0DC42). */
TEST(remote_with_no_payload)
{
    reset_state();
    call_send(-1, TEST_REMOTE, 0);
    ASSERT_EQ(-1, MSG_$WIRED_DATA.dpage_lock);
    ASSERT_EQ(0, os_copy_calls);
    ASSERT_EQ(0, copy_to_pa_calls);
    ASSERT_EQ(0, last_net_send.data_va);
    ASSERT_EQ(1, dump_data_calls);              /* the cleanup still runs */
}

/* NET_IO_$SEND's ten arguments, bracketed by lock 0x18 (0x00E0DC4A). */
TEST(net_io_send_arguments)
{
    reset_state();
    call_send(-1, TEST_REMOTE, 0x20);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x18, lock_id_seen);
    ASSERT_EQ(1, net_send_calls);
    ASSERT_EQ(TEST_PORT, last_net_send.port);
    ASSERT_EQ(TEST_HDR_VA, last_net_send.hdr_va);
    ASSERT_EQ(TEST_HDR_PA, last_net_send.hdr_pa);
    ASSERT_EQ(0x30, last_net_send.hdr_len);
    ASSERT_EQ(0x20, last_net_send.data_len);
    ASSERT_EQ(0, last_net_send.flags);
}

/* MSG_$SENDI dereferences everything and returns the SECOND info word. */
TEST(sendi_wrapper)
{
    uint32_t key = 0x0BADF00Du;
    uint32_t dnode = TEST_REMOTE;
    uint16_t dsock = 0x0031;
    int32_t src_or = 0x00000007;
    uint32_t snode = 0x00099999u;
    uint16_t ssock = 0x0052;
    uint16_t id = 0x00B6;
    uint16_t tlen = 4;
    uint16_t dlen = 0x10;
    uint16_t xmit = 0;
    status_$t st = 0;

    reset_state();
    memset(&caller_info, 0, sizeof(caller_info));
    caller_info.flags = 0x0002;

    MSG_$SENDI(&key, &dnode, &dsock, &src_or, &snode, &ssock, &caller_info,
               &id, template_buf, &tlen, payload_buf, &dlen, &xmit, &st);

    ASSERT_EQ(1, bld_calls);
    ASSERT_EQ(0x0BADF00Du, last_bld.routing_key);
    ASSERT_EQ(TEST_REMOTE, last_bld.dest_node);
    ASSERT_EQ(0x0031, last_bld.dest_sock);
    ASSERT_EQ(7, last_bld.src_node_or);
    ASSERT_EQ(0x00099999u, last_bld.src_node);
    ASSERT_EQ(0x0052, last_bld.src_sock);
    ASSERT_EQ(0x00B6, last_bld.request_id);
    ASSERT_EQ(4, last_bld.template_len);
    ASSERT_EQ(0x10, last_bld.data_len);
    ASSERT_EQ(0x0002 | MSG_INFO_FLAG_MSG, last_bld.info_flags);
    ASSERT_EQ(0x2222, xmit);                    /* net_info.xmit_status */
    ASSERT_EQ(status_$ok, st);
}

/*
 * MSG_$SEND takes its packet-info from MSG_$UNWIRED_DATA's template, overwrites the
 * flags word, and fixes port -1 / routing key 0 / source NODE_$ME.
 */
TEST(send_wrapper_uses_the_module_template)
{
    uint32_t dnode = TEST_REMOTE;
    uint16_t dsock = 0x0031;
    uint16_t ssock = 0x0052;
    uint16_t flags = 0x0810;
    uint16_t id = 0x00B6;
    uint16_t tlen = 4;
    uint16_t dlen = 0;
    uint16_t xmit = 0;
    status_$t st = 0;

    reset_state();
    /* a recognisable template: routing_type at +0x02, protocol at +0x06 */
    MSG_$UNWIRED_DATA.send_template.routing_type = PKT_ROUTING_INET;
    MSG_$UNWIRED_DATA.send_template.protocol = 0x8031;

    MSG_$SEND(&dnode, &dsock, &ssock, &flags, &id, template_buf, &tlen,
              payload_buf, &dlen, &xmit, &st);

    ASSERT_EQ(1, bld_calls);
    ASSERT_EQ(0, last_bld.routing_key);
    ASSERT_EQ(TEST_REMOTE, last_bld.dest_node);
    ASSERT_EQ(0x0031, last_bld.dest_sock);
    ASSERT_EQ(0, last_bld.src_node_or);
    ASSERT_EQ(TEST_NODE_ME, last_bld.src_node);
    ASSERT_EQ(0x0052, last_bld.src_sock);
    ASSERT_EQ(0x00B6, last_bld.request_id);
    /* the flags argument replaced the template's first word, then MSG's bit */
    ASSERT_EQ(0x0810 | MSG_INFO_FLAG_MSG, last_bld.info_flags);
    ASSERT_EQ(0x2222, xmit);
    ASSERT_EQ(status_$ok, st);
}

int main(void)
{
    printf("MSG_$$SEND / MSG_$SENDI / MSG_$SEND tests\n");

    RUN_TEST(template_too_long_allocates_nothing);
    RUN_TEST(packet_info_is_copied_with_the_msg_flag);
    RUN_TEST(header_builder_arguments);
    RUN_TEST(builder_failure_releases_the_header);
    RUN_TEST(builder_failure_with_port_minus_one_drops_the_dpage_count);
    RUN_TEST(explicit_port_overrides_and_forgives_unknown_network);
    RUN_TEST(auto_port_keeps_the_builders_choice);
    RUN_TEST(send_info_starts_from_the_port);
    RUN_TEST(local_delivery_builds_the_socket_record);
    RUN_TEST(local_delivery_refused);
    RUN_TEST(local_delivery_copy_failure);
    RUN_TEST(remote_uses_the_bounce_page);
    RUN_TEST(remote_falls_back_to_netbuf_pages);
    RUN_TEST(remote_copy_failure_skips_the_send);
    RUN_TEST(remote_with_no_payload);
    RUN_TEST(net_io_send_arguments);
    RUN_TEST(sendi_wrapper);
    RUN_TEST(send_wrapper_uses_the_module_template);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
