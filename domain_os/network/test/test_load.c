/*
 * network/test/test_load.c - NETWORK_$LOAD (0x00E2F7F2)
 *
 * Pins: an inactive ROUTE_$PORT_ARRAY[0] or a driver without an
 * attach_service entry does nothing; otherwise the entry gets
 * (&port->socket, the {0,0} record, 0, scratch, &status) and a bad status
 * crashes the system.
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

#include "network/network_internal.h"
#include "route/route.h"
#include "misc/crash_system.h"

route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
static route_$driver_info_t drv;
static int ncall, ncrash;
static status_$t next_status;
static uint16_t *seen_sock;
static const uint16_t *seen_rec;
static uint16_t seen_req;

void CRASH_SYSTEM(const status_$t *s) { (void)s; ncrash++; }

static int16_t attach_entry(uint16_t *sock, const uint16_t *rec, uint16_t req,
                            void *out4, status_$t *st)
{
    (void)out4;
    ncall++;
    seen_sock = sock;
    seen_rec = rec;
    seen_req = req;
    *st = next_status;
    return 0;
}

#include "../load.c"

static void setup(uint16_t active, int with_entry)
{
    uintptr_t a = (uintptr_t)&drv, f = (uintptr_t)&attach_entry;
    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    memset(&drv, 0, sizeof(drv));
    ARCH_HOST_VA_BASE = (a < f ? a : f) - 0x100;
    ROUTE_$PORT_ARRAY[0].active = active;
    ROUTE_$PORT_ARRAY[0].driver_info = ARCH_PTR_TO_VA(&drv);
    if (with_entry) {
        drv.attach_service = ARCH_PTR_TO_VA((void *)f);
    }
    ncall = ncrash = 0;
    next_status = 0;
}

TEST(inactive_port)
{
    setup(0, 1);
    NETWORK_$LOAD();
    ASSERT_EQ(0, ncall);
}

TEST(no_entry)
{
    setup(1, 0);
    NETWORK_$LOAD();
    ASSERT_EQ(0, ncall);
    ASSERT_EQ(0, ncrash);
}

TEST(entry_called_ok)
{
    setup(1, 1);
    NETWORK_$LOAD();
    ASSERT_EQ(1, ncall);
    ASSERT_EQ((uintptr_t)&ROUTE_$PORT_ARRAY[0].socket, (uintptr_t)seen_sock);
    ASSERT_EQ(0, seen_rec[0]);
    ASSERT_EQ(0, seen_rec[1]);
    ASSERT_EQ(0, seen_req);
    ASSERT_EQ(0, ncrash);
}

TEST(entry_fails_crash)
{
    setup(1, 1);
    next_status = 0x310002;
    NETWORK_$LOAD();
    ASSERT_EQ(1, ncrash);
}

int main(void)
{
    printf("NETWORK_$LOAD tests:\n");
    RUN_TEST(inactive_port);
    RUN_TEST(no_entry);
    RUN_TEST(entry_called_ok);
    RUN_TEST(entry_fails_crash);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
