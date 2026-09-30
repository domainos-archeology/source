/*
 * net_io/test/test_init.c - NET_IO_$INIT (0x00E31C98)
 *
 * Pins: an inactive port 0 is left alone; otherwise the driver's +0x14
 * entry is called with (&port->socket, &status), a bad status crashes, and
 * the port is marked active = 2.  (The nil-entry arm tests an unwritten
 * status and is not exercised.)
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
#include "misc/crash_system.h"

MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);

static struct {
    route_$port_t port;
    route_$driver_info_t drv;
} arena;
static int ncrash, ncall;
static status_$t crash_st, next_status;
static uint16_t *seen_sock;

void CRASH_SYSTEM(const status_$t *s) { ncrash++; crash_st = *s; }

static void start_entry(uint16_t *sock, status_$t *st)
{
    ncall++;
    seen_sock = sock;
    *st = next_status;
}

#include "../init.c"

static void setup(uint16_t active)
{
    memset(&arena, 0, sizeof(arena));
    {
        /* one base below both the arena and the driver entry, so each is a
         * 32-bit VA (text and data of one host program are close) */
        uintptr_t a = (uintptr_t)&arena, f = (uintptr_t)&start_entry;
        ARCH_HOST_VA_BASE = (a < f ? a : f) - 0x100;
    }
    ROUTE_$WIRED_DATA.portp[0] = &arena.port;
    arena.port.active = active;
    arena.port.driver_info = ARCH_PTR_TO_VA(&arena.drv);
    ncrash = ncall = 0;
    next_status = 0;
}

TEST(inactive_port_untouched)
{
    setup(0);
    NET_IO_$INIT();
    ASSERT_EQ(0, ncall);
    ASSERT_EQ(0, arena.port.active);
}

TEST(start_ok_marks_active)
{
    uintptr_t fn = (uintptr_t)&start_entry;
    setup(1);
    ASSERT_EQ(1, (fn - ARCH_HOST_VA_BASE) <= 0xFFFFFFFFu);
    ASSERT_EQ(1, ((uintptr_t)&arena - ARCH_HOST_VA_BASE) <= 0xFFFFFFFFu);
    arena.drv.leave_status_1 = ARCH_PTR_TO_VA((void *)fn);
    NET_IO_$INIT();
    ASSERT_EQ(1, ncall);
    ASSERT_EQ((uintptr_t)&arena.port.socket, (uintptr_t)seen_sock);
    ASSERT_EQ(0, ncrash);
    ASSERT_EQ(2, arena.port.active);
}

TEST(start_fails_crashes)
{
    uintptr_t fn = (uintptr_t)&start_entry;
    setup(1);
    ASSERT_EQ(1, (fn - ARCH_HOST_VA_BASE) <= 0xFFFFFFFFu);
    arena.drv.leave_status_1 = ARCH_PTR_TO_VA((void *)fn);
    next_status = 0x2B0003;
    NET_IO_$INIT();
    ASSERT_EQ(1, ncrash);
    ASSERT_EQ(0x2B0003, crash_st);
    ASSERT_EQ(2, arena.port.active);
}

int main(void)
{
    printf("NET_IO_$INIT tests:\n");
    RUN_TEST(inactive_port_untouched);
    RUN_TEST(start_ok_marks_active);
    RUN_TEST(start_fails_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
