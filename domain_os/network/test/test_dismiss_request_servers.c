/*
 * network/test/test_dismiss_request_servers.c -
 * NETWORK_$DISMISS_REQUEST_SERVERS (0x00E71E78)
 *
 * Pins the advance of the quit eventcount before the wait, and the wait's
 * two eventcounts and values (server count sign-extended, clock + 0x78).
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
#include "ec/ec.h"
#include "time/time.h"

ec_$eventcount_t NETWORK_$RQST_DONE_EC;
ec_$eventcount_t NETWORK_$RQST_QUIT_EC;
int16_t NETWORK_$REQUEST_SERVER_CNT;
uint32_t TIME_$CLOCKH;

static char trace[8];
static int nt;
static ec_$eventcount_t *adv;
static ec_$wait_ecs_t w_ecs;
static ec_$wait_vals_t w_vals;

void EC_$ADVANCE(ec_$eventcount_t *ec) { trace[nt++] = 'A'; adv = ec; }
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    trace[nt++] = 'W';
    w_ecs = ecs;
    w_vals = vals;
    return 0;
}

#include "../dismiss_request_servers.c"

TEST(advance_then_wait)
{
    NETWORK_$REQUEST_SERVER_CNT = -2;
    TIME_$CLOCKH = 0x1000;
    NETWORK_$DISMISS_REQUEST_SERVERS();
    ASSERT_EQ(0, strcmp(trace, "AW"));
    ASSERT_EQ((uintptr_t)&NETWORK_$RQST_QUIT_EC, (uintptr_t)adv);
    ASSERT_EQ((uintptr_t)&NETWORK_$RQST_DONE_EC, (uintptr_t)w_ecs.ec[0]);
    ASSERT_EQ((uintptr_t)&TIME_$CLOCKH, (uintptr_t)w_ecs.ec[1]);
    ASSERT_EQ(0, (uintptr_t)w_ecs.ec[2]);
    ASSERT_EQ((uint32_t)-2, (uint32_t)w_vals.val[0]);
    ASSERT_EQ(0x1078, w_vals.val[1]);
    ASSERT_EQ(0, w_vals.val[2]);
}

int main(void)
{
    printf("NETWORK_$DISMISS_REQUEST_SERVERS tests:\n");
    RUN_TEST(advance_then_wait);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
