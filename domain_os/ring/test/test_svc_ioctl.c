/*
 * ring/test/test_svc_ioctl.c - unit tests for RING_$SVC_IOCTL (0x00E776B8)
 * and the packet-type table helpers it uses: ring_$find_pkt_type
 * (0x00E7630C), ring_$pkt_type_overlaps (0x00E76352),
 * ring_$find_adjacent_pkt_type (0x00E7639E) and
 * ring_$find_overlapping_pkt_type (0x00E76422).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

#include "ring/ring_internal.h"

MODULE_DATA_DEFINE(ring_global_t, RING_$CTL, 0x00E86400);
MODULE_DATA_DEFINE(ring_$wired_data_t, RING_$WIRED_DATA, 0x00E261AC);

static int excl_start, excl_stop;

void ML_$EXCLUSION_START(ml_$exclusion_t *excl) { (void)excl; excl_start++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl) { (void)excl; excl_stop++; }

#include "../find_pkt_type.c"
#include "../pkt_type_overlaps.c"
#include "../find_adjacent_pkt_type.c"
#include "../find_overlapping_pkt_type.c"
#include "../svc_ioctl.c"

/* ---- helpers --------------------------------------------------------- */

#define CH 3

static uint32_t req_words[2 + 2 * 40];
#define REQ ((ring_$svc_ioctl_types_t *)(void *)req_words)

static uint16_t unit;
static ring_unit_t *u;

static void reset_state(void)
{
    memset(&RING_$CTL, 0, sizeof(RING_$CTL));
    memset(&RING_$WIRED_DATA, 0, sizeof(RING_$WIRED_DATA));
    memset(req_words, 0, sizeof(req_words));
    excl_start = excl_stop = 0;
    unit = 1;
    u = &RING_$CTL.units[1];
    u->state_flags = RING_UNIT_STARTED | RING_UNIT_RUNNING;
    RING_UNIT_CHANNEL(u, CH).flags = (boolean)0xFF;
    RING_UNIT_CHANNEL(u, 4).flags = (boolean)0xFF;
    REQ->channel = CH;
}

static status_$t ioctl_types(int16_t cmd, int n, const uint32_t *pairs)
{
    status_$t st = 0x99;
    int i;
    REQ->cmd = cmd;
    REQ->count = (int16_t)n;
    for (i = 0; i < n; i++) {
        REQ->ranges[i].low = pairs[2 * i];
        REQ->ranges[i].high = pairs[2 * i + 1];
    }
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    return st;
}

static void add_entry(uint32_t lo, uint32_t hi, int16_t ch)
{
    ring_pkt_type_t *e = &RING_UNIT_PKT_TYPE(u, u->pkt_type_cnt + 1);
    e->low = lo; e->high = hi; e->channel = ch;
    u->pkt_type_cnt++;
}

/* ---- helper routines ------------------------------------------------- */

static void test_find_pkt_type(void)
{
    add_entry(10, 20, CH);
    add_entry(30, 40, 4);
    ASSERT_EQ(1, ring_$find_pkt_type(10, u->pkt_types, u->pkt_type_cnt));
    ASSERT_EQ(2, ring_$find_pkt_type(40, u->pkt_types, u->pkt_type_cnt));
    ASSERT_EQ(0, ring_$find_pkt_type(25, u->pkt_types, u->pkt_type_cnt));
    ASSERT_EQ(0, ring_$find_pkt_type(10, u->pkt_types, 0));
}

static void test_overlap_helpers(void)
{
    add_entry(10, 20, CH);
    add_entry(30, 40, 4);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)ring_$pkt_type_overlaps(20, 25, u->pkt_types, 2));
    ASSERT_EQ(0, ring_$pkt_type_overlaps(21, 29, u->pkt_types, 2));
    ASSERT_EQ(2, ring_$find_overlapping_pkt_type(35, 50, u->pkt_types, 2));
    ASSERT_EQ(0, ring_$find_overlapping_pkt_type(41, 50, u->pkt_types, 2));
}

static void test_find_adjacent(void)
{
    int16_t idx = 1;
    add_entry(10, 20, CH);
    add_entry(30, 40, 4);
    add_entry(50, 60, CH);
    ring_$find_adjacent_pkt_type(u, &idx, CH, 21, 29, u->pkt_types, 3);
    ASSERT_EQ(1, idx);
    idx = 2;
    ring_$find_adjacent_pkt_type(u, &idx, CH, 41, 49, u->pkt_types, 3);
    ASSERT_EQ(3, idx);                         /* entry 2 is channel 4 */
    idx = 4;
    ring_$find_adjacent_pkt_type(u, &idx, CH, 21, 29, u->pkt_types, 3);
    ASSERT_EQ(0, idx);                         /* start past the end */
}

/* ---- RING_$SVC_IOCTL ------------------------------------------------- */

static void test_bad_unit(void)
{
    status_$t st = 0;
    unit = 2;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_invalid_unit_num, st);
}

static void test_offline_unit(void)
{
    status_$t st = 0;
    u->state_flags = 0;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_device_offline, st);

    u->state_flags = RING_UNIT_STARTED;        /* not running, not open */
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_device_offline, st);

    u->open_count = 1;                         /* open is enough */
    REQ->cmd = RING_IOCTL_NOP;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ok, st);
}

static void test_channel_not_open(void)
{
    status_$t st = 0;
    REQ->channel = 5;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_channel_not_open, st);
    REQ->channel = 11;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_channel_not_open, st);
    REQ->channel = 0;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_channel_not_open, st);
}

static void test_nop_and_unknown(void)
{
    status_$t st = 0x99;
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_NOP, 0, NULL));
    ASSERT_EQ(1, REQ->cmd);
    REQ->cmd = 5;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_not_implemented, st);
    REQ->cmd = 0;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_not_implemented, st);
}

static void test_get_stats(void)
{
    static ring_$stats_t out;
    ring_$svc_ioctl_stats_t *s = (ring_$svc_ioctl_stats_t *)(void *)REQ;
    status_$t st = 0;

    ARCH_HOST_VA_BASE = (uintptr_t)&out - 0x1000u;
    RING_$WIRED_DATA.stats[1].xmit_error = 0x1234;
    s->cmd = RING_IOCTL_GET_STATS;
    s->buf = 0x1000;
    s->buf_len = 0x3B;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ring_invalid_stats_block, st);

    s->buf_len = 0x3C;
    RING_$SVC_IOCTL(&unit, REQ, 0, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0x3C, s->cmd);
    ASSERT_EQ(0x1234, out.xmit_error);
}

static void test_add_version_mismatch(void)
{
    uint32_t p[2] = { 1, 2 };
    REQ->version = 2;
    ASSERT_EQ(status_$ring_driver_version_mismatch, ioctl_types(RING_IOCTL_ADD_TYPES, 1, p));
    ASSERT_EQ(status_$ring_driver_version_mismatch, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, p));
    ASSERT_EQ(0, excl_start);
}

static void test_add_new_extend_and_merge(void)
{
    uint32_t a[2] = { 10, 20 }, b[2] = { 21, 30 }, c[2] = { 40, 50 },
             d[2] = { 31, 39 };
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_ADD_TYPES, 1, a));
    ASSERT_EQ(1, u->pkt_type_cnt);
    ASSERT_EQ(CH, RING_UNIT_PKT_TYPE(u, 1).channel);
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_ADD_TYPES, 1, b));
    ASSERT_EQ(1, u->pkt_type_cnt);
    ASSERT_EQ(30, RING_UNIT_PKT_TYPE(u, 1).high);
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_ADD_TYPES, 1, c));
    ASSERT_EQ(2, u->pkt_type_cnt);
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_ADD_TYPES, 1, d));
    ASSERT_EQ(1, u->pkt_type_cnt);
    ASSERT_EQ(10, RING_UNIT_PKT_TYPE(u, 1).low);
    ASSERT_EQ(50, RING_UNIT_PKT_TYPE(u, 1).high);
    ASSERT_EQ(4, excl_start);
    ASSERT_EQ(4, excl_stop);
}

static void test_add_extends_low_end(void)
{
    uint32_t a[2] = { 5, 9 };
    add_entry(10, 20, CH);
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_ADD_TYPES, 1, a));
    ASSERT_EQ(5, RING_UNIT_PKT_TYPE(u, 1).low);
    ASSERT_EQ(20, RING_UNIT_PKT_TYPE(u, 1).high);
}

static void test_add_merge_moves_last_entry(void)
{
    uint32_t d[2] = { 21, 29 };
    add_entry(30, 40, CH);                     /* upper first */
    add_entry(10, 20, CH);
    add_entry(100, 200, 4);                    /* the last entry */
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_ADD_TYPES, 1, d));
    ASSERT_EQ(2, u->pkt_type_cnt);
    ASSERT_EQ(10, RING_UNIT_PKT_TYPE(u, 2).low);   /* the lower one grew */
    ASSERT_EQ(40, RING_UNIT_PKT_TYPE(u, 2).high);
    ASSERT_EQ(100, RING_UNIT_PKT_TYPE(u, 1).low);  /* last moved into 1 */
    ASSERT_EQ(4, RING_UNIT_PKT_TYPE(u, 1).channel);
}

static void test_add_errors(void)
{
    uint32_t bad[2] = { 20, 10 }, used[2] = { 15, 25 }, ok[2] = { 1, 2 };
    int i;
    add_entry(10, 20, 4);
    ASSERT_EQ(status_$ring_invalid_svc_packet_type, ioctl_types(RING_IOCTL_ADD_TYPES, 1, bad));
    ASSERT_EQ(status_$ring_pkt_type_in_use, ioctl_types(RING_IOCTL_ADD_TYPES, 1, used));
    ASSERT_EQ(1, u->pkt_type_cnt);
    ASSERT_EQ(2, excl_stop);

    for (i = 1; i < RING_MAX_PKT_TYPES; i++) {
        add_entry(1000u + 10u * i, 1000u + 10u * i + 1, 4);
    }
    ASSERT_EQ(status_$ring_no_channels, ioctl_types(RING_IOCTL_ADD_TYPES, 1, ok));
    ASSERT_EQ(RING_MAX_PKT_TYPES, u->pkt_type_cnt);
}

static void test_remove_whole_ends_and_split(void)
{
    uint32_t whole[2] = { 100, 200 }, lo[2] = { 10, 12 }, hi[2] = { 38, 40 },
             mid[2] = { 20, 25 };
    add_entry(10, 40, CH);
    add_entry(100, 200, CH);
    add_entry(300, 400, 4);
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, whole));
    ASSERT_EQ(2, u->pkt_type_cnt);
    ASSERT_EQ(300, RING_UNIT_PKT_TYPE(u, 2).low);  /* last moved into 2 */
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, lo));
    ASSERT_EQ(13, RING_UNIT_PKT_TYPE(u, 1).low);
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, hi));
    ASSERT_EQ(37, RING_UNIT_PKT_TYPE(u, 1).high);
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, mid));
    ASSERT_EQ(3, u->pkt_type_cnt);
    ASSERT_EQ(19, RING_UNIT_PKT_TYPE(u, 1).high);
    ASSERT_EQ(26, RING_UNIT_PKT_TYPE(u, 3).low);
    ASSERT_EQ(37, RING_UNIT_PKT_TYPE(u, 3).high);
    ASSERT_EQ(CH, RING_UNIT_PKT_TYPE(u, 3).channel);
}

static void test_remove_errors(void)
{
    uint32_t none[2] = { 50, 60 }, wide[2] = { 5, 15 }, other[2] = { 300, 310 },
             bad[2] = { 9, 8 };
    add_entry(10, 40, CH);
    add_entry(300, 400, 4);
    ASSERT_EQ(status_$ring_pkt_type_not_in_use, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, none));
    ASSERT_EQ(status_$ring_pkt_type_not_in_use, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, wide));
    ASSERT_EQ(status_$ring_pkt_type_not_in_use, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, other));
    ASSERT_EQ(status_$ring_invalid_svc_packet_type, ioctl_types(RING_IOCTL_REMOVE_TYPES, 1, bad));
    ASSERT_EQ(2, u->pkt_type_cnt);
    ASSERT_EQ(4, excl_stop);
}

static void test_zero_count_is_ok(void)
{
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_ADD_TYPES, 0, NULL));
    ASSERT_EQ(status_$ok, ioctl_types(RING_IOCTL_REMOVE_TYPES, 0, NULL));
    ASSERT_EQ(2, excl_stop);
}

int main(void)
{
    printf("RING_$SVC_IOCTL tests:\n");
    RUN_TEST(find_pkt_type);
    RUN_TEST(overlap_helpers);
    RUN_TEST(find_adjacent);
    RUN_TEST(bad_unit);
    RUN_TEST(offline_unit);
    RUN_TEST(channel_not_open);
    RUN_TEST(nop_and_unknown);
    RUN_TEST(get_stats);
    RUN_TEST(add_version_mismatch);
    RUN_TEST(add_new_extend_and_merge);
    RUN_TEST(add_extends_low_end);
    RUN_TEST(add_merge_moves_last_entry);
    RUN_TEST(add_errors);
    RUN_TEST(remove_whole_ends_and_split);
    RUN_TEST(remove_errors);
    RUN_TEST(zero_count_is_ok);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
