/*
 * net_io/test/test_put_in_sock_common.c - unit tests for
 * net_io_$put_in_sock_common (0x00E0E238)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

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
#include "netlog/netlog.h"
#include "node/node.h"
#include "ring/ring.h"
#include "ring/ringlog.h"

/* ---- data ------------------------------------------------------------ */

MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
MODULE_DATA_DEFINE(route_$rtwired_data_t, ROUTE_$RTWIRED_DATA, 0x00E87D80);
MODULE_DATA_DEFINE(ringlog_ctl_t, RINGLOG_$CTL, 0x00E2C32C);
uint32_t NODE_$ME;
uint32_t NET_IO_$ALL_F_ADDR = 0x000FFFFF;
uint16_t RING_$OVERFLOW_OVERFLOW;
uint16_t RING_$FILE_OVERFLOW;
uint16_t RING_$DELIVERY_FAILED;
int8_t NETLOG_$OK_TO_LOG_SERVER;
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

static uint8_t arena[0x100] __attribute__((aligned(16)));
static route_$port_t port;

/* ---- mocks ----------------------------------------------------------- */

static int n_log, n_find, n_crash, n_put, n_put_int, n_rtn, n_dump;
static route_$port_t *find_ret;
static int8_t put_ret[4];
static uint16_t put_sock[4];
static int8_t put_flag[4];
static sock_$pkt_info_t put_rec;
static ec_$eventcount_t **put_ec;
static int16_t dump_len;
static uint8_t log_byte0;

int16_t RINGLOG_$LOGIT(const uint8_t *info, void *pkt)
{
    (void)pkt; n_log++; log_byte0 = info[0]; return 0;
}
route_$port_t *ROUTE_$FIND_PORTP(uint16_t network, int32_t socket)
{
    (void)network; (void)socket; n_find++; return find_ret;
}
static jmp_buf crash_jmp;
static status_$t crash_status;
void CRASH_SYSTEM(const status_$t *status_p)
{
    n_crash++; crash_status = *status_p; longjmp(crash_jmp, 1);
}
void TIME_$ABS_CLOCK(clock_t *clock) { clock->high = 0x11223344; clock->low = 0x5566; }
int8_t SOCK_$PUT(uint16_t sock_num, sock_$pkt_info_t *pkt_info, int8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2)
{
    (void)ec_param1; (void)ec_param2;
    int k = n_put + n_put_int;
    put_sock[k] = sock_num; put_flag[k] = flags; put_rec = *pkt_info;
    n_put++;
    return put_ret[k];
}
int8_t SOCK_$PUT_INT(uint16_t sock_num, sock_$pkt_info_t *pkt_info,
                     int8_t flags, uint16_t ec_param1, uint16_t ec_param2,
                     ec_$eventcount_t **ec_ret)
{
    (void)ec_param1; (void)ec_param2;
    int k = n_put + n_put_int;
    put_sock[k] = sock_num; put_flag[k] = flags; put_rec = *pkt_info;
    put_ec = ec_ret;
    n_put_int++;
    return put_ret[k];
}
void NETBUF_$RTN_HDR(uint32_t *va_ptr) { (void)va_ptr; n_rtn++; }
void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len) { (void)buffers; n_dump++; dump_len = len; }

#include "../put_in_sock_common.c"

/* ---- helpers --------------------------------------------------------- */

static uint32_t hdr_va;
static uint32_t pages[4];
static pkt_$hdr_t *hdr(void) { return (pkt_$hdr_t *)(void *)arena; }

static void reset_state(void)
{
    memset(arena, 0, sizeof(arena));
    memset(&port, 0, sizeof(port));
    memset(&RINGLOG_$CTL, 0, sizeof(RINGLOG_$CTL));
    memset(&ROUTE_$RTWIRED_DATA, 0, sizeof(ROUTE_$RTWIRED_DATA));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x10000u;
    hdr_va = 0x10000u;
    pages[0] = 0x1000; pages[1] = 0x2000; pages[2] = 0; pages[3] = 0;
    n_log = n_find = n_crash = n_put = n_put_int = n_rtn = n_dump = 0;
    memset(put_ret, 0, sizeof(put_ret));
    find_ret = &port;
    NODE_$ME = 0x1234;
    ROUTE_$WIRED_DATA.sock = 9;
    RING_$OVERFLOW_OVERFLOW = RING_$FILE_OVERFLOW = RING_$DELIVERY_FAILED = 0;
    NETLOG_$OK_TO_LOG_SERVER = 0;
    TIME_$CLOCKH = 0xCAFE;
    port.network = 0x77;
    /* internet header to this node, socket 5 */
    hdr()->routing_type = 2;
    hdr()->u.inet.dest.node = 0xAB001234;
    hdr()->u.inet.dest.sock = 5;
    hdr()->u.inet.src.node = 0x42;
}

static int8_t call(route_$port_t *p, int8_t int_level)
{
    return net_io_$put_in_sock_common(p, 3, 0x20, int_level, &hdr_va, pages,
                                      0x30, 0x400, (ec_$eventcount_t **)0x99);
}

/* ---- tests ----------------------------------------------------------- */

static void test_queued_to_dest_socket(void)
{
    put_ret[0] = (int8_t)0xFF;
    RINGLOG_$CTL.logging_active = (int8_t)0xFF;
    ASSERT_EQ(0xFF, (uint8_t)call(&port, 0));
    ASSERT_EQ(1, n_log);
    ASSERT_EQ(0xFF, log_byte0);
    ASSERT_EQ(0, n_find);
    ASSERT_EQ(1, n_put);
    ASSERT_EQ(5, put_sock[0]);
    ASSERT_EQ(0xFF, (uint8_t)put_flag[0]);
    ASSERT_EQ(0x10000, put_rec.hdr);
    ASSERT_EQ(0x30, put_rec.hdr_len);
    ASSERT_EQ(0x400, put_rec.data_len);
    ASSERT_EQ(0x2000, put_rec.data_pages[1]);
    ASSERT_EQ(0x11223344, put_rec.src_addr);
    ASSERT_EQ(0x5566, put_rec.src_port);
    ASSERT_EQ(0xCAFE, hdr()->dest_node);          /* TIME_$CLOCKH stamp */
    ASSERT_EQ(0x77, hdr()->u.inet.src.net);       /* filled from the port */
    ASSERT_EQ(1, port.stat_long_54);
    ASSERT_EQ(0, n_rtn);
}

static void test_full_clock_stamp_and_int_level(void)
{
    put_ret[0] = (int8_t)0xFF;
    NETLOG_$OK_TO_LOG_SERVER = (int8_t)0xFF;
    hdr()->info_flags = 0x12;
    hdr()->zero_05[2] = 0x9A;
    ASSERT_EQ(0xFF, (uint8_t)call(&port, (int8_t)0xFF));
    ASSERT_EQ(1, n_put_int);
    ASSERT_EQ(0x99, (uintptr_t)put_ec);
    ASSERT_EQ(0x11223344, hdr()->dest_node);
    ASSERT_EQ(0x12, hdr()->info_flags);
    ASSERT_EQ(0x55, hdr()->zero_05[0]);
    ASSERT_EQ(0x66, hdr()->zero_05[1]);
    ASSERT_EQ(0x9A, hdr()->zero_05[2]);
}

static void test_find_port(void)
{
    put_ret[0] = (int8_t)0xFF;
    ASSERT_EQ(0xFF, (uint8_t)call(NULL, 0));
    ASSERT_EQ(1, n_find);
    ASSERT_EQ(0, n_crash);
    ASSERT_EQ(1, port.stat_long_54);
}

static void test_no_port_crashes(void)
{
    find_ret = NULL;
    if (setjmp(crash_jmp) == 0) {
        call(NULL, 0);
    }
    ASSERT_EQ(1, n_crash);
    ASSERT_EQ(0x002B0003, crash_status);
    ASSERT_EQ(0, n_put);
}

static void test_foreign_goes_to_router(void)
{
    hdr()->u.inet.dest.node = 0x999;
    ASSERT_EQ(0, (uint8_t)call(&port, 0));
    ASSERT_EQ(9, put_sock[0]);
    ASSERT_EQ(0, (uint8_t)put_flag[0]);           /* ~0xFF */
    ASSERT_EQ(1, ROUTE_$RTWIRED_DATA.q_oflo);
    ASSERT_EQ(1, n_rtn);
    ASSERT_EQ(1, n_dump);
    ASSERT_EQ(0x400, dump_len);
}

static void test_router_sock_minus_one_counts_delivery_failed(void)
{
    ROUTE_$WIRED_DATA.sock = 0xFFFF;
    hdr()->u.inet.dest.node = 0x999;
    pages[0] = 0;
    ASSERT_EQ(0, (uint8_t)call(&port, 0));
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.q_oflo);
    ASSERT_EQ(1, RING_$DELIVERY_FAILED);
    ASSERT_EQ(0, n_dump);
}

static void test_file_socket_overflow(void)
{
    hdr()->u.inet.dest.sock = 2;
    put_ret[1] = (int8_t)0xFF;
    ASSERT_EQ(0xFF, (uint8_t)call(&port, 0));
    ASSERT_EQ(2, n_put);
    ASSERT_EQ(6, put_sock[1]);
    ASSERT_EQ(1, RING_$FILE_OVERFLOW);
    ASSERT_EQ(0, RING_$OVERFLOW_OVERFLOW);
    ASSERT_EQ(0, n_rtn);
}

static void test_file_socket_overflow_full(void)
{
    hdr()->u.inet.dest.sock = 2;
    ASSERT_EQ(0, (uint8_t)call(&port, (int8_t)0xFF));
    ASSERT_EQ(2, n_put_int);
    ASSERT_EQ(1, RING_$FILE_OVERFLOW);
    ASSERT_EQ(1, RING_$OVERFLOW_OVERFLOW);
    ASSERT_EQ(1, n_rtn);
}

static void test_all_ones_source_dropped(void)
{
    hdr()->u.inet.src.node = 0xFF0FFFFF;
    ASSERT_EQ(0, (uint8_t)call(&port, 0));
    ASSERT_EQ(0, n_put);
    ASSERT_EQ(0, port.stat_long_54);
    ASSERT_EQ(1, n_rtn);
    ASSERT_EQ(1, n_dump);
}

static void test_broadcast_socket4_dropped(void)
{
    hdr()->dest_node = 2;
    hdr()->info_flags = 0x80;
    hdr()->u.inet.dest.sock = 4;
    hdr()->zero_05[2] = 0x08;
    ASSERT_EQ(0, (uint8_t)call(&port, 0));
    ASSERT_EQ(0, n_put);
    ASSERT_EQ(1, n_rtn);
    /* bit 3 clear: delivered */
    reset_state();
    hdr()->dest_node = 2;
    hdr()->info_flags = 0x80;
    hdr()->u.inet.dest.sock = 4;
    put_ret[0] = (int8_t)0xFF;
    ASSERT_EQ(0xFF, (uint8_t)call(&port, 0));
    ASSERT_EQ(4, put_sock[0]);
}

static void test_local_routing_route_word(void)
{
    uint16_t *w = (uint16_t *)(void *)(arena + 0x1C);
    hdr()->routing_type = 1;
    hdr()->route_count = 1;
    w[2] = 0x33;                                  /* +0x20 */
    put_ret[0] = (int8_t)0xFF;
    ASSERT_EQ(0xFF, (uint8_t)call(&port, 0));
    ASSERT_EQ(2, hdr()->route_count);
    ASSERT_EQ(0x33, put_sock[0]);
}

int main(void)
{
    printf("net_io_$put_in_sock_common tests\n");
    RUN_TEST(queued_to_dest_socket);
    RUN_TEST(full_clock_stamp_and_int_level);
    RUN_TEST(find_port);
    RUN_TEST(no_port_crashes);
    RUN_TEST(foreign_goes_to_router);
    RUN_TEST(router_sock_minus_one_counts_delivery_failed);
    RUN_TEST(file_socket_overflow);
    RUN_TEST(file_socket_overflow_full);
    RUN_TEST(all_ones_source_dropped);
    RUN_TEST(broadcast_socket4_dropped);
    RUN_TEST(local_routing_route_word);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
