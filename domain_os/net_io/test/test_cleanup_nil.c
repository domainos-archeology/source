/*
 * net_io/test/test_cleanup_nil.c - NET_IO_$CLEANUP_NIL (0x00E74EC8)
 *
 * Pins: nothing happens unless port_asid[socket] equals asid; otherwise
 * ROUTE_$PORTP[socket] is shortened and ROUTE_$SERVICE gets the shared
 * operation cell (value 8).
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
net_io_unwired_t NET_IO_UNWIRED;
const uint16_t net_io_$route_op_close = 0x0008;

static route_$port_t ports[2];
static route_$port_t *short_arg;
static route_$short_port_t *short_out;
static const uint16_t *svc_op;
static route_$short_port_t *svc_info;
static int nshort, nsvc;

void ROUTE_$SHORT_PORT(route_$port_t *p, route_$short_port_t *s)
{
    short_arg = p;
    short_out = s;
    nshort++;
}
void ROUTE_$SERVICE(const uint16_t *op, route_$short_port_t *info,
                    status_$t *st)
{
    svc_op = op;
    svc_info = info;
    *st = 0;
    nsvc++;
}

#include "../cleanup_nil.c"

TEST(other_asid_noop)
{
    uint16_t sock = 1;
    nshort = nsvc = 0;
    NET_IO_UNWIRED.port_asid[1] = 5;
    NET_IO_$CLEANUP_NIL(&sock, 6);
    ASSERT_EQ(0, nshort);
    ASSERT_EQ(0, nsvc);
}

TEST(matching_asid_closes)
{
    uint16_t sock = 1;
    nshort = nsvc = 0;
    ROUTE_$WIRED_DATA.portp[1] = &ports[1];
    NET_IO_UNWIRED.port_asid[1] = 5;
    NET_IO_$CLEANUP_NIL(&sock, 5);
    ASSERT_EQ(1, nshort);
    ASSERT_EQ(1, nsvc);
    ASSERT_EQ((uintptr_t)&ports[1], (uintptr_t)short_arg);
    ASSERT_EQ((uintptr_t)short_out, (uintptr_t)svc_info);
    ASSERT_EQ((uintptr_t)&net_io_$route_op_close, (uintptr_t)svc_op);
    ASSERT_EQ(8, *svc_op);
}

int main(void)
{
    printf("NET_IO_$CLEANUP_NIL tests:\n");
    RUN_TEST(other_asid_noop);
    RUN_TEST(matching_asid_closes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
