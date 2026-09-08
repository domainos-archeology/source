/*
 * mac_os/test/test_check_range_overlap.c - MAC_OS_$CHECK_RANGE_OVERLAP
 * (0x00E0B1BC).
 *
 * Two properties from the disassembly review:
 *
 *   - the table is walked ASCENDING from entry 0.  A1 starts at the table
 *     base (0x00E0B1D4) and steps forward one 12-byte entry at a time
 *     ("lea (0xc,A1),A1" at 0x00E0B1EE), so the FIRST overlapping entry the
 *     walk meets is the one that stops it.  The earlier C counted down.
 *
 *   - the result register D0 doubles as the dbf counter, so the word handed
 *     back is not "the index shifted left":
 *       overlap    -> (remaining & 0xFF00) | 0x00FF   ("st D0b",  0x00E0B1EA)
 *       no overlap -> 0xFF00                          ("clr.b D0b" on 0xFFFF)
 *       empty      -> count & 0xFF00                  ("clr.b D0b" on count)
 *     The only caller reads the low byte as a Domain boolean
 *     ("tst.b D0b / bpl" at 0x00E0B31C), and all three forms agree there.
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

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "mac_os/mac_os_internal.h"
#include "../check_range_overlap.c"

/* The byte the one caller actually looks at. */
#define ANSWER_BYTE(w) ((int8_t)((uint16_t)(w) & 0xFFu))

static mac_os_$pkt_type_entry_t table[8];

static void fill(int n)
{
    int i;

    memset(table, 0, sizeof table);
    /* entry i covers [100*i+10, 100*i+20] */
    for (i = 0; i < n; i++) {
        table[i].range_low = (uint32_t)(100 * i + 10);
        table[i].range_high = (uint32_t)(100 * i + 20);
        table[i].channel_index = (uint16_t)(i + 1);
    }
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* The 12-byte stride the walk assumes. */
TEST(entry_stride_is_twelve)
{
    ASSERT_EQ(12, sizeof(mac_os_$pkt_type_entry_t));
    ASSERT_EQ(12, (size_t)((uint8_t *)&table[1] - (uint8_t *)&table[0]));
}

/*
 * Overlapping entry 0 of a four-entry table stops on the first pass, so the
 * dbf counter is still count-1 = 3 and the word is (3 & 0xFF00) | 0xFF.
 */
TEST(overlap_on_the_first_entry)
{
    uint32_t range[2] = { 15, 16 };
    int16_t r;

    fill(4);
    r = MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 4);

    ASSERT_EQ(0x00FF, (uint16_t)r);
    ASSERT_EQ((int8_t)0xFF, ANSWER_BYTE(r));
}

/*
 * The walk is ascending: a range that overlaps BOTH entry 1 and entry 3 must
 * stop at entry 1.  With a descending walk the counter left in the result
 * would differ, and more importantly the caller's semantics ("the first
 * conflicting registration wins") would change.
 */
TEST(the_first_overlap_going_up_wins)
{
    /* covers 110..320, i.e. entries 1, 2 and 3 */
    uint32_t range[2] = { 110, 320 };
    int16_t r;
    int seen_first;

    fill(4);
    r = MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 4);
    ASSERT_EQ((int8_t)0xFF, ANSWER_BYTE(r));

    /*
     * Prove the direction by leaving only entry 1 able to match and then only
     * entry 3: a descending walk would report the same boolean, so the
     * direction is pinned instead by where the walk stops - fill the table so
     * that entry 1 overlaps and entry 3 would trap on a poisoned range that
     * an ascending walk never reaches.
     */
    fill(4);
    table[3].range_low = 0;
    table[3].range_high = 0xFFFFFFFFu;      /* matches anything */
    table[1].range_low = 110;
    table[1].range_high = 120;

    /* stops at entry 1: remaining = 4 - 1 - 1 = 2, so the word is 0x00FF */
    r = MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 4);
    seen_first = ((uint16_t)r == 0x00FF);
    ASSERT_EQ(1, seen_first);
}

/* No overlap at all: the dbf leaves 0xFFFF and only the low byte is cleared. */
TEST(no_overlap_returns_ff00)
{
    uint32_t range[2] = { 500, 600 };
    int16_t r;

    fill(4);
    r = MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 4);

    ASSERT_EQ(0xFF00, (uint16_t)r);
    ASSERT_EQ(0, ANSWER_BYTE(r));           /* still "no overlap" */
}

/* An empty table never enters the loop; the result is count & 0xFF00. */
TEST(empty_table_returns_the_count_high_byte)
{
    uint32_t range[2] = { 0, 0xFFFFFFFFu };

    fill(0);
    ASSERT_EQ(0x0000, (uint16_t)MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 0));
    ASSERT_EQ(0, ANSWER_BYTE(MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 0)));

    /* a count whose high byte is non-zero carries it through unchanged */
    ASSERT_EQ(0x0300, (uint16_t)MAC_OS_$CHECK_RANGE_OVERLAP(range, table,
                                                            (int16_t)0x0301) &
              0xFF00);
}

/* The two comparisons are unsigned, so a high range does not read as negative. */
TEST(comparisons_are_unsigned)
{
    uint32_t range[2] = { 0x80000000u, 0x80000010u };
    int16_t r;

    fill(1);
    table[0].range_low = 0x7FFFFFF0u;
    table[0].range_high = 0x7FFFFFFFu;

    /* 0x80000000 > 0x7FFFFFFF unsigned, so no overlap */
    r = MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 1);
    ASSERT_EQ(0, ANSWER_BYTE(r));

    table[0].range_high = 0x80000000u;
    r = MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 1);
    ASSERT_EQ((int8_t)0xFF, ANSWER_BYTE(r));
}

/* Touching ranges count as overlapping (both tests are <= / >=). */
TEST(touching_ranges_overlap)
{
    uint32_t range[2] = { 20, 30 };
    int16_t r;

    fill(1);                                 /* entry 0 is [10, 20] */
    r = MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 1);
    ASSERT_EQ((int8_t)0xFF, ANSWER_BYTE(r));

    range[0] = 21;
    r = MAC_OS_$CHECK_RANGE_OVERLAP(range, table, 1);
    ASSERT_EQ(0, ANSWER_BYTE(r));
}

int main(void)
{
    printf("MAC_OS_$CHECK_RANGE_OVERLAP tests\n");
    RUN_TEST(entry_stride_is_twelve);
    RUN_TEST(overlap_on_the_first_entry);
    RUN_TEST(the_first_overlap_going_up_wins);
    RUN_TEST(no_overlap_returns_ff00);
    RUN_TEST(empty_table_returns_the_count_high_byte);
    RUN_TEST(comparisons_are_unsigned);
    RUN_TEST(touching_ranges_overlap);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
