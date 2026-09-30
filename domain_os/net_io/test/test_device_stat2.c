/*
 * net_io/test/test_device_stat2.c - NET_IO_$DEVICE_STAT2 (0x00E5A420)
 *
 * Pins: *stat_len_ret is zeroed first; no port answers UNKNOWN_$NETWORK_UID
 * and status_$internet_unknown_network_port; a port copies its driver's
 * network UID and then either answers status_$ok (nil get_stats2 slot) or calls
 * the slot with the address of the index argument and the rest forwarded.
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
#include "uid/uid.h"

uid_t UNKNOWN_$NETWORK_UID = { 0x705, 0 };

static struct {
    route_$port_t port;
    net_io_$driver_t drv;
} arena;
static int port_exists;
static uint16_t find_net;
static int32_t find_idx;
static int ncall;
static uint16_t seen_index, seen_max;
static void *seen_buf;
static uint16_t *seen_len;
static status_$t *seen_st;

route_$port_t *ROUTE_$FIND_PORTP(uint16_t network, int32_t socket)
{
    find_net = network;
    find_idx = socket;
    return port_exists ? &arena.port : NULL;
}

static void stat_entry(uint16_t *index_ptr, void *stat_buf, uint16_t max_len,
                       uint16_t *stat_len_ret, status_$t *status_ret)
{
    ncall++;
    seen_index = *index_ptr;
    seen_buf = stat_buf;
    seen_max = max_len;
    seen_len = stat_len_ret;
    seen_st = status_ret;
    *stat_len_ret = 0x3C;
    *status_ret = 0x12345;
}

#include "../device_stat2.c"

static uid_t id;
static uint16_t len;
static status_$t st;
static uint8_t buf[0x80];

static void setup(int exists)
{
    memset(&arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)&arena - 0x100;
    arena.port.driver_info = ARCH_PTR_TO_VA(&arena.drv);
    arena.drv.network_uid.high = 0x704;
    arena.drv.network_uid.low = 0x99;
    port_exists = exists;
    ncall = 0;
    id.high = id.low = 0xDEAD;
    len = 0x7777;
    st = 1;
}

TEST(no_port)
{
    setup(0);
    NET_IO_$DEVICE_STAT2(3, 0xFFFF, 0x80, &id, buf, &len, &st);
    ASSERT_EQ(3, find_net);
    ASSERT_EQ((uint32_t)-1, (uint32_t)find_idx);   /* ext.l: sign-extended */
    ASSERT_EQ(0, len);
    ASSERT_EQ(0x705, id.high);
    ASSERT_EQ(0, id.low);
    ASSERT_EQ(status_$internet_unknown_network_port, st);
}

TEST(nil_slot_ok)
{
    setup(1);
    arena.drv.get_stats = (net_io_$driver_fn_t)stat_entry;  /* the other slot */
    NET_IO_$DEVICE_STAT2(1, 2, 0x80, &id, buf, &len, &st);
    ASSERT_EQ(0, ncall);
    ASSERT_EQ(0, len);
    ASSERT_EQ(0x704, id.high);
    ASSERT_EQ(0x99, id.low);
    ASSERT_EQ(status_$ok, st);
}

TEST(slot_called)
{
    setup(1);
    arena.drv.get_stats2 = (net_io_$driver_fn_t)stat_entry;
    NET_IO_$DEVICE_STAT2(1, 2, 0x80, &id, buf, &len, &st);
    ASSERT_EQ(1, ncall);
    ASSERT_EQ(2, seen_index);
    ASSERT_EQ(0x80, seen_max);
    ASSERT_EQ((uintptr_t)buf, (uintptr_t)seen_buf);
    ASSERT_EQ((uintptr_t)&len, (uintptr_t)seen_len);
    ASSERT_EQ((uintptr_t)&st, (uintptr_t)seen_st);
    ASSERT_EQ(0x3C, len);
    ASSERT_EQ(0x12345, st);
    ASSERT_EQ(0x704, id.high);
}

int main(void)
{
    printf("NET_IO_$DEVICE_STAT2 tests:\n");
    RUN_TEST(no_port);
    RUN_TEST(nil_slot_ok);
    RUN_TEST(slot_called);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
