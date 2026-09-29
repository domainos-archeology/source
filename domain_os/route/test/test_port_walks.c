/*
 * route/test/test_port_walks.c - the two ascending walks of source-w2sh.
 *
 *   ROUTE_$CLEANUP_WIRED  (0x00E69B7C).  The unwire loop sets its cursor to
 *     &pages[1] ("lea (0x4,A0),A2" at 0x00E69BA6), reads at -4 from it
 *     ("move.l (-0x4,A2),-(SP)" at 0x00E69BAC) and steps FORWARD
 *     ("addq.l #0x4,A2" at 0x00E69BB8), so the pages come off the array in
 *     index order 0,1,2,...  The earlier C counted down.
 *
 *   ROUTE_$VALIDATE_PORT  (0x00E65904).  The port scan starts at
 *     &ROUTE_$PORTP[0] ("movea.l #0xe26ee8,A0" at 0x00E65932) and steps
 *     forward ("addq.l #0x4,A0" at 0x00E6594A), so with two active ports on
 *     the same network the LOWER index is chosen.  The earlier C scanned
 *     7..0 and picked the higher one.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

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
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Globals and mocks
 * ============================================================================ */

#include "route/route_internal.h"

MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
MODULE_DATA_DEFINE(route_$rtwired_data_t, ROUTE_$RTWIRED_DATA, 0x00E87D80);

#define MAX_UNWIRES 16
static uint32_t unwired[MAX_UNWIRES];
static int unwire_count;

void WP_$UNWIRE(uint32_t wired_addr)
{
    if (unwire_count < MAX_UNWIRES) {
        unwired[unwire_count] = wired_addr;
    }
    unwire_count++;
}

#include "../cleanup_wired.c"
#include "../validate_port.c"

/* ============================================================================
 * ROUTE_$CLEANUP_WIRED
 * ============================================================================ */

static void reset_wired(void)
{
    unsigned i;

    unwire_count = 0;
    memset(unwired, 0, sizeof unwired);
    ROUTE_$WIRED_DATA.routing = 0;
    ROUTE_$RTWIRED_DATA.n_user_ports = 0;
    for (i = 0; i < ROUTE_$MAX_WIRED_PAGES; i++) {
        ROUTE_$RTWIRED_DATA.wired_pages[i] = 0x00B00000u + i;
    }
}

TEST(cleanup_wired_walks_up_from_element_zero)
{
    reset_wired();
    ROUTE_$RTWIRED_DATA.n_wired_pages = 4;

    ROUTE_$CLEANUP_WIRED();

    ASSERT_EQ(4, unwire_count);
    ASSERT_EQ(0x00B00000u, unwired[0]);
    ASSERT_EQ(0x00B00001u, unwired[1]);
    ASSERT_EQ(0x00B00002u, unwired[2]);
    ASSERT_EQ(0x00B00003u, unwired[3]);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.n_wired_pages);     /* 0x00E69BBE */
}

/* 0x00E69B9A subq.w / bmi: a zero count still clears the counter. */
TEST(cleanup_wired_with_no_pages)
{
    reset_wired();
    ROUTE_$RTWIRED_DATA.n_wired_pages = 0;

    ROUTE_$CLEANUP_WIRED();

    ASSERT_EQ(0, unwire_count);
    ASSERT_EQ(0, ROUTE_$RTWIRED_DATA.n_wired_pages);
}

/* 0x00E69B84 / 0x00E69B8C: either guard aborts before any unwiring. */
TEST(cleanup_wired_is_gated)
{
    reset_wired();
    ROUTE_$RTWIRED_DATA.n_wired_pages = 3;
    ROUTE_$RTWIRED_DATA.n_user_ports = 1;
    ROUTE_$CLEANUP_WIRED();
    ASSERT_EQ(0, unwire_count);
    ASSERT_EQ(3, ROUTE_$RTWIRED_DATA.n_wired_pages);

    reset_wired();
    ROUTE_$RTWIRED_DATA.n_wired_pages = 3;
    ROUTE_$WIRED_DATA.routing = (boolean)0xFF;
    ROUTE_$CLEANUP_WIRED();
    ASSERT_EQ(0, unwire_count);
    ASSERT_EQ(3, ROUTE_$RTWIRED_DATA.n_wired_pages);
}

/* ============================================================================
 * ROUTE_$VALIDATE_PORT
 * ============================================================================ */

static route_$port_t ports[ROUTE_$MAX_PORTS];
static route_$driver_info_t drivers[ROUTE_$MAX_PORTS];

static void reset_ports(void)
{
    unsigned i;

    memset(ports, 0, sizeof ports);
    memset(drivers, 0, sizeof drivers);
    /*
     * driver_info holds a 32-bit target virtual address; point the host
     * arena base below the driver array so the offsets stored in the field
     * are non-zero (see arch/host/arch.h).
     */
    ARCH_HOST_VA_BASE = (uintptr_t)drivers - 0x1000u;
    for (i = 0; i < ROUTE_$MAX_PORTS; i++) {
        ROUTE_$WIRED_DATA.portp[i] = &ports[i];
        ports[i].driver_info = ARCH_PTR_TO_VA(&drivers[i]);
    }
}

/*
 * Two active ports carry the same network.  The image takes the first one it
 * meets walking up, i.e. the lower index; its driver record is the one whose
 * flags decide the answer.
 */
TEST(validate_port_picks_the_lower_index)
{
    reset_ports();
    ports[2].active = 1;
    ports[2].network = 0x11223344u;
    drivers[2].flags = 0x02;            /* routable */
    ports[5].active = 1;
    ports[5].network = 0x11223344u;
    drivers[5].flags = 0x00;            /* NOT routable */

    /* the lower index is routable, so the answer is 1 */
    ASSERT_EQ(1, ROUTE_$VALIDATE_PORT(0x11223344, 0));

    /* flip the two records: now the lower index refuses */
    drivers[2].flags = 0x00;
    drivers[5].flags = 0x02;
    ASSERT_EQ(2, ROUTE_$VALIDATE_PORT(0x11223344, 0));
}

/* An inactive port is skipped even when its network matches (0x00E6593A). */
TEST(validate_port_skips_inactive_ports)
{
    reset_ports();
    ports[1].active = 0;
    ports[1].network = 0x55u;
    drivers[1].flags = 0x00;
    ports[6].active = 1;
    ports[6].network = 0x55u;
    drivers[6].flags = 0x02;

    ASSERT_EQ(1, ROUTE_$VALIDATE_PORT(0x55, 0));
}

/*
 * No match: a local query (the Domain boolean TRUE, high bit set) reports 2,
 * anything else reports 1 (0x00E65956-0x00E6595A).
 */
TEST(validate_port_no_match)
{
    reset_ports();

    ASSERT_EQ(2, ROUTE_$VALIDATE_PORT(0x99, (int8_t)0xFF));
    ASSERT_EQ(1, ROUTE_$VALIDATE_PORT(0x99, 0));
}

/* routing_key == 0 short-circuits to port 0 (0x00E6591A). */
TEST(validate_port_zero_key_uses_port_zero)
{
    reset_ports();

    ports[0].active = 0;
    ASSERT_EQ(0, ROUTE_$VALIDATE_PORT(0, 0));

    ports[0].active = 1;
    drivers[0].flags = 0x02;
    ASSERT_EQ(1, ROUTE_$VALIDATE_PORT(0, 0));

    drivers[0].flags = 0x00;
    ASSERT_EQ(2, ROUTE_$VALIDATE_PORT(0, 0));
}

int main(void)
{
    printf("ROUTE_$ port-walk tests\n");
    RUN_TEST(cleanup_wired_walks_up_from_element_zero);
    RUN_TEST(cleanup_wired_with_no_pages);
    RUN_TEST(cleanup_wired_is_gated);
    RUN_TEST(validate_port_picks_the_lower_index);
    RUN_TEST(validate_port_skips_inactive_ports);
    RUN_TEST(validate_port_no_match);
    RUN_TEST(validate_port_zero_key_uses_port_zero);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
