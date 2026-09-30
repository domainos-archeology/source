/*
 * net_io/test/test_send.c - unit tests for NET_IO_$SEND (0x00E0E692)
 */

#include <stdio.h>
#include <stdint.h>
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

#include "net_io/net_io_internal.h"
#include "node/node.h"
#include "ring/ringlog.h"

/* ---- data ------------------------------------------------------------ */

MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
MODULE_DATA_DEFINE(ringlog_ctl_t, RINGLOG_$CTL, 0x00E2C32C);
MODULE_DATA_DEFINE(ringlog_$data_t, RINGLOG_$DATA, 0x00EA3E38);
route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
uint32_t NODE_$ME;
uint32_t NET_IO_$ALL_F_ADDR = 0x000FFFFF;

static uint8_t arena[0x400] __attribute__((aligned(16)));
static route_$port_t port3;
static net_io_$driver_t drv;

/* ---- mocks ----------------------------------------------------------- */

static int n_copy, n_deliver, n_send, n_log;
static status_$t copy_status, send_status;
static route_$port_t *deliver_port;
static uint16_t send_flags;
static uint16_t send_sock;
static const uint8_t *log_info;

void NET_IO_$COPY_PACKET(uint32_t *hdr_src_p, uint16_t hdr_len,
                         uint32_t src_data_va, uint32_t *src_pages,
                         uint16_t data_len, uint32_t *hdr_va_out,
                         uint32_t *data_pages_out, status_$t *status_ret)
{
    (void)hdr_src_p; (void)hdr_len; (void)src_data_va; (void)src_pages;
    (void)data_len; (void)data_pages_out;
    n_copy++; *hdr_va_out = 0x1234; *status_ret = copy_status;
}
void net_io_$put_in_sock_common(route_$port_t *port, uint16_t port_type,
                                uint16_t socket, int8_t flag,
                                uint32_t *hdr_va_p, uint32_t *data_pa_p,
                                uint16_t hdr_len, uint16_t data_len, void *out)
{
    (void)port_type; (void)socket; (void)flag; (void)hdr_va_p; (void)data_pa_p;
    (void)hdr_len; (void)data_len; (void)out;
    n_deliver++; deliver_port = port;
}
int16_t RINGLOG_$LOGIT(const uint8_t *info, void *pkt)
{
    (void)pkt; n_log++; log_info = info; return 3;
}
static void mock_sendp(uint16_t *socket, uint32_t hdr_pa, void *hdr,
                       uint16_t hdr_len, const uint32_t *data_pages,
                       uint32_t data_va, uint16_t data_len,
                       const uint16_t *flags, uint16_t *xmit_status,
                       status_$t *status_ret)
{
    (void)hdr_pa; (void)hdr; (void)hdr_len; (void)data_pages; (void)data_va;
    (void)data_len;
    n_send++; send_sock = *socket; send_flags = *flags;
    *xmit_status = 0x0042;
    *status_ret = send_status;
}

#include "../send.c"

/* ---- helpers --------------------------------------------------------- */

static pkt_$hdr_t *hdr(void) { return (pkt_$hdr_t *)(void *)arena; }

static void reset_state(void)
{
    memset(arena, 0, sizeof(arena));
    memset(&port3, 0, sizeof(port3));
    memset(&drv, 0, sizeof(drv));
    memset(&RINGLOG_$CTL, 0, sizeof(RINGLOG_$CTL));
    memset(&RINGLOG_$DATA, 0, sizeof(RINGLOG_$DATA));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x10000u;
    ROUTE_$WIRED_DATA.portp[3] = &port3;
    port3.active = 2;
    port3.port_type = 7;
    port3.socket = 0x55;
    port3.network = 0x100;
    port3.driver_info = ARCH_PTR_TO_VA(&drv);
    drv.sendp = (net_io_$driver_fn_t)mock_sendp;
    NODE_$ME = 0x777;
    n_copy = n_deliver = n_send = n_log = 0;
    copy_status = send_status = 0;
    hdr()->dest_node = 0x888;
    hdr()->routing_type = 1;
}

static void send(int16_t port, status_$t *st, net_io_$send_info_t *info)
{
    uint32_t hva = 0x10000;
    uint32_t pages[4] = { 0 };
    NET_IO_$SEND(port, &hva, 0x4000, 0x30, 0, pages, 0x100, 0x0009, info, st);
}

/* ---- tests ----------------------------------------------------------- */

static void test_bad_port(void)
{
    status_$t st;
    net_io_$send_info_t info;
    send(8, &st, &info);
    ASSERT_EQ(0x002B0003, st);
    send(-1, &st, &info);
    ASSERT_EQ(0x002B0003, st);
}

static void test_port_states(void)
{
    status_$t st;
    net_io_$send_info_t info;
    port3.active = 0;
    send(3, &st, &info);
    ASSERT_EQ(0x002B0001, st);
    port3.active = 1;
    send(3, &st, &info);
    ASSERT_EQ(0x0011000E, st);
}

static void test_loopback(void)
{
    status_$t st;
    net_io_$send_info_t info = { 0x99, 0x0001 };
    hdr()->dest_node = 0x777;
    send(3, &st, &info);
    ASSERT_EQ(1, n_copy);
    ASSERT_EQ(1, n_deliver);
    ASSERT_EQ((unsigned long)&ROUTE_$PORT_ARRAY[0], (unsigned long)deliver_port);
    ASSERT_EQ(0, info.port_net);
    ASSERT_EQ(0x8009, info.xmit_status);        /* or-ed, not stored */
    ASSERT_EQ(0, n_send);
}

static void test_local_send_logged(void)
{
    status_$t st;
    net_io_$send_info_t info;
    RINGLOG_$CTL.logging_active = (int8_t)0xFF;
    send_status = 0x00110004;
    send(3, &st, &info);
    ASSERT_EQ(1, n_send);
    ASSERT_EQ(0x55, send_sock);
    ASSERT_EQ(0x0009, send_flags);
    ASSERT_EQ(7, info.port_net);
    ASSERT_EQ(0x0042, info.xmit_status);
    ASSERT_EQ(0x00110004, st);
    ASSERT_EQ(1, port3.forward_count);
    ASSERT_EQ(0x00, log_info[0]);
    ASSERT_EQ(0x0042, *(uint16_t *)(void *)&RINGLOG_$DATA.bytes[3 * 0x2E + 0x2E]);
}

static void test_nil_sendp(void)
{
    status_$t st = 5;
    net_io_$send_info_t info;
    drv.sendp = NULL;
    send(3, &st, &info);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, n_log);
}

static void test_inet_bad_source(void)
{
    status_$t st;
    net_io_$send_info_t info;
    hdr()->routing_type = 2;
    hdr()->u.inet.src.node = 0x0FFFFF;
    send(3, &st, &info);
    ASSERT_EQ(0x00110023, st);
    ASSERT_EQ(0, n_send);
}

static void test_inet_broadcast_on_net_delivers_too(void)
{
    status_$t st;
    net_io_$send_info_t info;
    hdr()->routing_type = 2;
    hdr()->u.inet.dest.net = 0x100;
    hdr()->u.inet.dest.node = 0xFFFFFF;
    hdr()->u.inet.info_0b = 1;
    copy_status = 0x77;                         /* ignored */
    send(3, &st, &info);
    ASSERT_EQ(0, hdr()->dest_node);
    ASSERT_EQ(0x80, hdr()->info_flags);
    ASSERT_EQ(1, n_copy);
    ASSERT_EQ(0, n_deliver);                    /* copy failed */
    ASSERT_EQ(1, n_send);
    ASSERT_EQ(0, st);
}

static void test_inet_off_net_undoes_broadcast(void)
{
    status_$t st;
    net_io_$send_info_t info;
    hdr()->routing_type = 2;
    hdr()->u.inet.dest.net = 0x200;
    hdr()->u.inet.dest.node = 0x12;
    hdr()->info_flags = 0x81;
    send(3, &st, &info);
    ASSERT_EQ(0x01, hdr()->info_flags);
    ASSERT_EQ(0xFFFFFF, hdr()->u.inet.dest.node);
    ASSERT_EQ(1, n_send);
}

int main(void)
{
    printf("NET_IO_$SEND tests:\n");
    RUN_TEST(bad_port);
    RUN_TEST(port_states);
    RUN_TEST(loopback);
    RUN_TEST(local_send_logged);
    RUN_TEST(nil_sendp);
    RUN_TEST(inet_bad_source);
    RUN_TEST(inet_broadcast_on_net_delivers_too);
    RUN_TEST(inet_off_net_undoes_broadcast);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
