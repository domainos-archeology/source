/*
 * ring/test/test_svc_close.c - RING_$SVC_CLOSE (0x00E76E22)
 *
 * Pins the unit and channel checks, the exclusion pairing, the OS-socket
 * exemption, the packet-type compaction (last entry moves down, 10 bytes)
 * and the open_count decrement.
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

static char trace[64];
static int ntrace;
static uint16_t closed_sock;

void ML_$EXCLUSION_START(ml_$exclusion_t *e)
{
    trace[ntrace++] = (e == &RING_$CTL.units[1].rx_exclusion ||
                       e == &RING_$CTL.units[0].rx_exclusion) ? 'R' : 'T';
}
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)
{
    trace[ntrace++] = (e == &RING_$CTL.units[1].rx_exclusion ||
                       e == &RING_$CTL.units[0].rx_exclusion) ? 'r' : 't';
}
void SOCK_$CLOSE(uint16_t s) { closed_sock = s; trace[ntrace++] = 'S'; }

#include "../svc_close.c"

static ring_unit_t *u;

static void setup(void)
{
    memset(&RING_$CTL, 0, sizeof(RING_$CTL));
    memset(trace, 0, sizeof(trace));
    ntrace = 0;
    closed_sock = 0;
    u = &RING_$CTL.units[1];
    u->open_count = 3;
    RING_UNIT_CHANNEL(u, 4).flags = -1;
    RING_UNIT_CHANNEL(u, 4).socket_id = 0x33;
    /* pkt types: 1 -> ch 4, 2 -> ch 2, 3 -> ch 4, 4 -> ch 4 */
    u->pkt_type_cnt = 4;
    RING_UNIT_PKT_TYPE(u, 1) = (ring_pkt_type_t){ 0x10, 0x11, 4, 0x1111 };
    RING_UNIT_PKT_TYPE(u, 2) = (ring_pkt_type_t){ 0x20, 0x21, 2, 0x2222 };
    RING_UNIT_PKT_TYPE(u, 3) = (ring_pkt_type_t){ 0x30, 0x31, 4, 0x3333 };
    RING_UNIT_PKT_TYPE(u, 4) = (ring_pkt_type_t){ 0x40, 0x41, 4, 0x4444 };
}

TEST(bad_unit)
{
    uint16_t unit = 2, ch = 4;
    status_$t st = 0;
    setup();
    RING_$SVC_CLOSE(&unit, &ch, 0, 0, &st);
    ASSERT_EQ(status_$ring_invalid_unit_num, st);
    ASSERT_EQ(0, ntrace);
}

TEST(channel_not_open)
{
    uint16_t unit = 1, ch = 5;
    status_$t st = 0;
    setup();
    RING_$SVC_CLOSE(&unit, &ch, 0, 0, &st);
    ASSERT_EQ(status_$ring_channel_not_open, st);
    ASSERT_EQ(0, strcmp(trace, "Rr"));
    ch = 0;
    RING_$SVC_CLOSE(&unit, &ch, 0, 0, &st);
    ch = 11;
    RING_$SVC_CLOSE(&unit, &ch, 0, 0, &st);
    ASSERT_EQ(status_$ring_channel_not_open, st);
    ASSERT_EQ(3, u->open_count);
}

TEST(close_compacts_pkt_types)
{
    uint16_t unit = 1, ch = 4;
    status_$t st = 1;
    setup();
    RING_$SVC_CLOSE(&unit, &ch, 0, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, strcmp(trace, "RSrTt"));
    ASSERT_EQ(0x33, closed_sock);
    ASSERT_EQ(0, (uint8_t)RING_UNIT_CHANNEL(u, 4).flags);
    ASSERT_EQ(2, u->open_count);
    /* entry 1 (ch4) <- entry 4 (ch4), recheck, <- entry 3 (ch4), recheck,
     * <- entry 2 (ch2); count 1.  Pad word of entry 1 untouched. */
    ASSERT_EQ(1, u->pkt_type_cnt);
    ASSERT_EQ(0x20, RING_UNIT_PKT_TYPE(u, 1).low);
    ASSERT_EQ(2, RING_UNIT_PKT_TYPE(u, 1).channel);
    ASSERT_EQ(0x1111, (uint16_t)RING_UNIT_PKT_TYPE(u, 1)._pad0a);
}

TEST(os_socket_not_closed)
{
    uint16_t unit = 1, ch = 4;
    status_$t st = 1;
    setup();
    RING_UNIT_CHANNEL(u, 4).socket_id = RING_OS_SOCKET_ID;
    RING_$SVC_CLOSE(&unit, &ch, 0, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, strcmp(trace, "RrTt"));
}

int main(void)
{
    printf("RING_$SVC_CLOSE tests:\n");
    RUN_TEST(bad_unit);
    RUN_TEST(channel_not_open);
    RUN_TEST(close_compacts_pkt_types);
    RUN_TEST(os_socket_not_closed);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
