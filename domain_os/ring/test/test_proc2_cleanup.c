/*
 * ring/test/test_proc2_cleanup.c - RING_$PROC2_CLEANUP (0x00E76A42)
 *
 * Pins: unit above 1 and a unit without RING_UNIT_STARTED do nothing; only
 * open channels owned by the asid are closed; the OS socket is not closed;
 * owned packet types are compacted; open_count drops by the number closed.
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

#include "ring/ring_internal.h"

MODULE_DATA_DEFINE(ring_global_t, RING_$CTL, 0x00E86400);

static int nexcl;
static int nsock;
static uint16_t socks[16];

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; nexcl++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e) { (void)e; nexcl++; }
void SOCK_$CLOSE(uint16_t s) { socks[nsock++] = s; }

#include "../proc2_cleanup.c"

static ring_unit_t *u;

static void setup(void)
{
    memset(&RING_$CTL, 0, sizeof(RING_$CTL));
    nexcl = 0;
    nsock = 0;
    u = &RING_$CTL.units[0];
    u->state_flags = RING_UNIT_STARTED;
    u->open_count = 5;
    /* ch1: owned, socket 0x21; ch3: other asid; ch10: owned, OS socket;
     * ch6: owned but not open */
    RING_UNIT_CHANNEL(u, 1) = (ring_channel_t){ -1, 0, 9, 0x21, 0 };
    RING_UNIT_CHANNEL(u, 3) = (ring_channel_t){ -1, 0, 8, 0x23, 0 };
    RING_UNIT_CHANNEL(u, 6) = (ring_channel_t){ 0, 0, 9, 0x26, 0 };
    RING_UNIT_CHANNEL(u, 10) = (ring_channel_t){ -1, 0, 9, RING_OS_SOCKET_ID, 0 };
    u->pkt_type_cnt = 3;
    RING_UNIT_PKT_TYPE(u, 1) = (ring_pkt_type_t){ 1, 1, 10, 0 };
    RING_UNIT_PKT_TYPE(u, 2) = (ring_pkt_type_t){ 2, 2, 3, 0 };
    RING_UNIT_PKT_TYPE(u, 3) = (ring_pkt_type_t){ 3, 3, 1, 0 };
}

TEST(bad_unit_noop)
{
    uint16_t unit = 2;
    setup();
    RING_$PROC2_CLEANUP(&unit, 9);
    ASSERT_EQ(0, nexcl);
    ASSERT_EQ(5, u->open_count);
}

TEST(not_started_noop)
{
    uint16_t unit = 0;
    setup();
    u->state_flags = RING_UNIT_RUNNING;
    RING_$PROC2_CLEANUP(&unit, 9);
    ASSERT_EQ(0, nexcl);
    ASSERT_EQ((uint8_t)-1, (uint8_t)RING_UNIT_CHANNEL(u, 1).flags);
}

TEST(closes_owned_channels)
{
    uint16_t unit = 0;
    setup();
    RING_$PROC2_CLEANUP(&unit, 9);
    ASSERT_EQ(4, nexcl);
    ASSERT_EQ(1, nsock);
    ASSERT_EQ(0x21, socks[0]);
    ASSERT_EQ(0, (uint8_t)RING_UNIT_CHANNEL(u, 1).flags);
    ASSERT_EQ(0, (uint8_t)RING_UNIT_CHANNEL(u, 10).flags);
    ASSERT_EQ((uint8_t)-1, (uint8_t)RING_UNIT_CHANNEL(u, 3).flags);
    ASSERT_EQ(3, u->open_count);
    /* ch1 drops entry 3 (entry 3 moves onto itself, count 2);
     * ch10 drops entry 1 (entry 2 moves down, count 1) */
    ASSERT_EQ(1, u->pkt_type_cnt);
    ASSERT_EQ(2, RING_UNIT_PKT_TYPE(u, 1).low);
    ASSERT_EQ(3, RING_UNIT_PKT_TYPE(u, 1).channel);
}

int main(void)
{
    printf("RING_$PROC2_CLEANUP tests:\n");
    RUN_TEST(bad_unit_noop);
    RUN_TEST(not_started_noop);
    RUN_TEST(closes_owned_channels);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
