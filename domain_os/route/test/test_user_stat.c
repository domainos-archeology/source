/*
 * Tests for the ROUTE_$USER_STAT record array (0xE87FD6 .. 0xE88216).
 *
 * These pin the layout and the record addressing that NET_IO_$CREATE_PORT
 * uses (0x00E5A5C2 - 0x00E5A682) and that ROUTE_$CLOSE_PORT undoes
 * (0x00E69F9A).  Nothing here calls kernel code; the point is that the record
 * stride, the record body and the array span keep matching the SAU2 link map
 * and the instructions quoted in route/route_internal.h.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "route/route_internal.h"

static int tests_run = 0;
static int tests_failed = 0;

#define RUN_TEST(fn)                                                          \
    do {                                                                      \
        tests_run++;                                                          \
        printf("  Running %-28s ", #fn);                                      \
        if (fn() == 0) {                                                      \
            printf("PASSED\n");                                               \
        } else {                                                              \
            tests_failed++;                                                   \
        }                                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long _e = (long)(expected), _a = (long)(actual);                      \
        if (_e != _a) {                                                       \
            printf("FAILED\n    %s:%d: %s == 0x%lx, expected 0x%lx\n",        \
                   __FILE__, __LINE__, #actual, _a, _e);                      \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/*
 * The scan loop at 0x00E5A5CC/0x00E5A5E2 advances by 0x90 per record and the
 * dbf at 0x00E5A5E6 runs four times; 4 * 0x90 is the whole map symbol.
 */
static int test_record_stride_and_span(void)
{
    ASSERT_EQ(0x90, sizeof(route_$user_stat_t));
    ASSERT_EQ(4, ROUTE_$MAX_USER_STATS);
    ASSERT_EQ(0x240, sizeof(route_$user_stat_t) * ROUTE_$MAX_USER_STATS);
    ASSERT_EQ(0xE88216 - 0xE87FD6,
              sizeof(route_$user_stat_t) * ROUTE_$MAX_USER_STATS);
    return 0;
}

/*
 * The record body is route_$port_stats_t; byte 0 is the in-use boolean and
 * the counters sit on odd word boundaries, so the record has to stay packed.
 */
static int test_record_body_offsets(void)
{
    ASSERT_EQ(0x00, offsetof(route_$user_stat_t, stats));
    ASSERT_EQ(0x02, offsetof(route_$user_stat_t, stats.deep_queue_puts));
    ASSERT_EQ(0x06, offsetof(route_$user_stat_t, stats.failed_puts));
    ASSERT_EQ(0x0A, offsetof(route_$user_stat_t, stats.queue_depth));
    ASSERT_EQ(0x8E, offsetof(route_$user_stat_t, _tail_8e));
    ASSERT_EQ(0x8E, sizeof(route_$port_stats_t));
    return 0;
}

/*
 * NET_IO_$CREATE_PORT stores ROUTE_$USER_STAT + n*0x90 - 0x90 into the port
 * entry, with n the 1-based record number D1 ends the scan on
 * (0x00E5A656 - 0x00E5A668).  Reproduce that arithmetic exactly, including
 * the n<<4 + n<<3<<4 shape the compiler emitted for the multiply.
 */
static int test_one_based_record_address(void)
{
    route_$user_stat_t base[ROUTE_$MAX_USER_STATS];
    int n;

    for (n = 1; n <= ROUTE_$MAX_USER_STATS; n++) {
        unsigned long d0 = (unsigned long)n << 4;      /* 0x00E5A658 */
        unsigned long d1 = d0 << 3;                    /* 0x00E5A65C */
        unsigned long off = d0 + d1;                   /* 0x00E5A65E: n*0x90 */
        unsigned char *rec = (unsigned char *)base + off - 0x90; /* 0x00E5A664 */

        ASSERT_EQ((unsigned long)n * 0x90, off);
        ASSERT_EQ((unsigned long)(unsigned char *)&base[n - 1],
                  (unsigned long)rec);
    }
    return 0;
}

/*
 * The allocator picks the first record whose byte 0 is not a Domain true, and
 * "st (A1)" marks it.  ROUTE_$CLOSE_PORT's "clr.b (A0)" gives it back.
 */
static int test_in_use_scan(void)
{
    route_$user_stat_t recs[ROUTE_$MAX_USER_STATS];
    unsigned char *bytes = (unsigned char *)recs;
    int chosen;
    int n;

    memset(recs, 0, sizeof(recs));
    bytes[0 * 0x90] = 0xFF;     /* st: records 1 and 2 already taken */
    bytes[1 * 0x90] = 0xFF;

    chosen = 0;
    for (n = 1; n <= ROUTE_$MAX_USER_STATS; n++) {
        if ((signed char)bytes[(n - 1) * 0x90] >= 0) {   /* tst.b / bmi */
            chosen = n;
            break;
        }
    }
    ASSERT_EQ(3, chosen);

    bytes[(chosen - 1) * 0x90] = 0xFF;
    ASSERT_EQ(0xFF, bytes[2 * 0x90]);

    bytes[(chosen - 1) * 0x90] = 0x00;                   /* clr.b (A0) */
    ASSERT_EQ(0x00, bytes[2 * 0x90]);
    return 0;
}

/*
 * ORIGINAL BUG (bead source-2km0): the clear loop at 0x00E5A66C - 0x00E5A67E
 * writes offsets 0x00..0x90 inclusive, one byte past the record.  This test
 * documents the overrun rather than a fixed loop; if the record ever grows to
 * 0x91 bytes the overrun would disappear and this test should be revisited.
 */
static int test_clear_loop_overruns_by_one(void)
{
    unsigned char arena[2 * 0x90];
    int d0;

    memset(arena, 0xAA, sizeof(arena));
    for (d0 = 0; d0 <= 0x90; d0++) {        /* move.w #0x90,D1w through dbf */
        arena[d0] = 0;
    }
    ASSERT_EQ(0x91, 0x90 + 1);
    ASSERT_EQ(0x00, arena[0x8F]);           /* last byte of record 1 */
    ASSERT_EQ(0x00, arena[0x90]);           /* first byte of record 2: clobbered */
    ASSERT_EQ(0xAA, arena[0x91]);
    return 0;
}

int main(void)
{
    printf("ROUTE_$USER_STAT layout tests\n");
    RUN_TEST(test_record_stride_and_span);
    RUN_TEST(test_record_body_offsets);
    RUN_TEST(test_one_based_record_address);
    RUN_TEST(test_in_use_scan);
    RUN_TEST(test_clear_loop_overruns_by_one);
    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
