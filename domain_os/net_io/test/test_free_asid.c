/*
 * net_io/test/test_free_asid.c - NET_IO_$FREE_ASID (0x00E74E84): the eight
 * ports, the active and nil-slot tests, the (&socket, asid) call; and
 * NET_IO_$PUT_IN_SOCK (0x00E0E4A0)'s forwarding to the common routine.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "net_io/net_io_internal.h"

MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);

static uint16_t *cleanup_sock[8];
static uint16_t cleanup_asid[8];
static int ncleanup;
static void cleanup_a(uint16_t *s, uint16_t a)
{ cleanup_sock[ncleanup] = s; cleanup_asid[ncleanup++] = a; }

static int ncommon;
static route_$port_t *c_port;
static uint16_t c_type, c_sock, c_hlen, c_dlen;
static int8_t c_int;
static uint32_t *c_hdr, *c_data;
static ec_$eventcount_t **c_ec;
int8_t net_io_$put_in_sock_common(route_$port_t *port, uint16_t port_type,
                                  uint16_t socket, int8_t int_level,
                                  uint32_t *hdr_va_p, uint32_t *data_pa_p,
                                  uint16_t hdr_len, uint16_t data_len,
                                  ec_$eventcount_t **ec_ret)
{
    ncommon++;
    c_port = port; c_type = port_type; c_sock = socket; c_int = int_level;
    c_hdr = hdr_va_p; c_data = data_pa_p; c_hlen = hdr_len; c_dlen = data_len;
    c_ec = ec_ret;
    return -1;
}

#include "../free_asid.c"
#include "../put_in_sock.c"

static route_$port_t ports[8];
static net_io_$driver_t drvs[2];
#define drv_with drvs[0]
#define drv_without drvs[1]

TEST(calls_active_ports_with_a_cleanup_slot)
{
    int i;
    memset(ports, 0, sizeof(ports));
    memset(&drv_with, 0, sizeof(drv_with));
    memset(&drv_without, 0, sizeof(drv_without));
    ARCH_HOST_VA_BASE = (uintptr_t)&drvs[0] - 0x10;
    drv_with.proc2_cleanup = (net_io_$driver_fn_t)cleanup_a;
    for (i = 0; i < 8; i++) {
        ROUTE_$WIRED_DATA.portp[i] = &ports[i];
        ports[i].driver_info = ARCH_PTR_TO_VA(&drv_with);
    }
    ports[1].active = 1;
    ports[4].active = 0x28;
    ports[7].active = 1;
    ports[7].driver_info = ARCH_PTR_TO_VA(&drv_without);
    ncleanup = 0;
    NET_IO_$FREE_ASID(0x33);
    ASSERT_EQ(2, ncleanup);
    ASSERT_EQ((uintptr_t)&ports[1].socket, (uintptr_t)cleanup_sock[0]);
    ASSERT_EQ((uintptr_t)&ports[4].socket, (uintptr_t)cleanup_sock[1]);
    ASSERT_EQ(0x33, cleanup_asid[1]);
}

TEST(put_in_sock_forwards_with_nil_port_and_process_level)
{
    uint32_t hdr = 0x1000, data = 0x2000;
    ncommon = 0;
    NET_IO_$PUT_IN_SOCK(2, 0x15, &hdr, &data, 0x40, 0x400);
    ASSERT_EQ(1, ncommon);
    ASSERT_EQ(0, (uintptr_t)c_port);
    ASSERT_EQ(2, c_type);
    ASSERT_EQ(0x15, c_sock);
    ASSERT_EQ(0, c_int);
    ASSERT_EQ((uintptr_t)&hdr, (uintptr_t)c_hdr);
    ASSERT_EQ((uintptr_t)&data, (uintptr_t)c_data);
    ASSERT_EQ(0x40, c_hlen);
    ASSERT_EQ(0x400, c_dlen);
}

int main(void)
{
    printf("NET_IO_$FREE_ASID / NET_IO_$PUT_IN_SOCK\n");
    RUN_TEST(calls_active_ports_with_a_cleanup_slot);
    RUN_TEST(put_in_sock_forwards_with_nil_port_and_process_level);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
