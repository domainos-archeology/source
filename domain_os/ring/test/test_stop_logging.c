/*
 * ring/test/test_stop_logging.c - RINGLOG_$STOP_LOGGING (0x00E721CC).
 *
 * The properties under test are the two the disassembly review flagged:
 *
 *   - the unwire loop touches wired_pages[0..wire_count-1], not
 *     [1..wire_count].  The effective address at 0x00E721FE is
 *     "(-0x4,A3,D0w*0x1)" with A3 = 0xE2C32C and D0 = index*4, and the index
 *     runs 1..wire_count, so the -4 shifts the whole walk down one element.
 *     RINGLOG_$CNTL fills the same array from element 0 ("pea (A1)" at
 *     0x00E722C6).
 *   - the loop index lives in the CALLER's frame word at (-0x2,A6)
 *     (0x00E721DA movea.l (A6),A2), so the flattened parameter is left
 *     holding wire_count + 1 when the loop ran.
 *
 * Also pinned: the dbf at 0x00E7220E runs the body wire_count times, a zero
 * or negative wire_count skips it, and a cleared RING_$LOGGING_NOW makes the
 * whole routine a no-op (0x00E721DC bpl).
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
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Globals and mocks
 * ============================================================================ */

#include "ring/ringlog_internal.h"

MODULE_DATA_DEFINE(ringlog_ctl_t, RINGLOG_$CTL, 0x00E2C32C);
MODULE_DATA_DEFINE(ringlog_$data_t, RINGLOG_$DATA, 0x00EA3E38);

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

#include "../stop_logging.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static void reset(void)
{
    unsigned i;

    memset(&RINGLOG_$CTL, 0, sizeof(RINGLOG_$CTL));
    memset(unwired, 0, sizeof(unwired));
    unwire_count = 0;

    /* Element n gets the recognisable value 0xPA0000 + n. */
    for (i = 0; i < 10; i++) {
        RINGLOG_$CTL.wired_pages[i] = 0x00A00000u + i;
    }
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* The walk starts at element 0 and stops one short of wire_count. */
TEST(unwires_elements_zero_through_count_minus_one)
{
    int16_t parent_index = 0x7777;

    reset();
    RINGLOG_$CTL.logging_active = (int8_t)0xFF;
    RINGLOG_$CTL.wire_count = 3;

    RINGLOG_$STOP_LOGGING(&parent_index);

    ASSERT_EQ(3, unwire_count);
    ASSERT_EQ(0x00A00000u, unwired[0]);
    ASSERT_EQ(0x00A00001u, unwired[1]);
    ASSERT_EQ(0x00A00002u, unwired[2]);
    /* 0x00E72218 clr.w (0x30,A0) */
    ASSERT_EQ(0, RINGLOG_$CTL.wire_count);
    /* 0x00E721E2 clr.b (0x38,A0) */
    ASSERT_EQ(0, RINGLOG_$CTL.logging_active);
    /* the parent's word ran 1..3 and was left one past the end */
    ASSERT_EQ(4, parent_index);
}

/* The full array: index 1..10 selects elements 0..9, never element 10. */
TEST(a_full_array_stops_at_element_nine)
{
    int16_t parent_index = 0;

    reset();
    RINGLOG_$CTL.logging_active = (int8_t)0xFF;
    RINGLOG_$CTL.wire_count = 10;

    RINGLOG_$STOP_LOGGING(&parent_index);

    ASSERT_EQ(10, unwire_count);
    ASSERT_EQ(0x00A00000u, unwired[0]);
    ASSERT_EQ(0x00A00009u, unwired[9]);
    ASSERT_EQ(11, parent_index);
}

/* 0x00E721EA subq.w #0x1 / bmi: a zero count skips the loop but still clears. */
TEST(zero_wire_count_unwires_nothing)
{
    int16_t parent_index = 0x1234;

    reset();
    RINGLOG_$CTL.logging_active = (int8_t)0xFF;
    RINGLOG_$CTL.wire_count = 0;

    RINGLOG_$STOP_LOGGING(&parent_index);

    ASSERT_EQ(0, unwire_count);
    ASSERT_EQ(0, RINGLOG_$CTL.logging_active);
    ASSERT_EQ(0, RINGLOG_$CTL.wire_count);
    ASSERT_EQ(0x1234, parent_index);    /* the parent's word is untouched */
}

/* One page: the single unwire is element 0. */
TEST(one_wired_page_unwires_element_zero)
{
    int16_t parent_index = 0;

    reset();
    RINGLOG_$CTL.logging_active = (int8_t)0xFF;
    RINGLOG_$CTL.wire_count = 1;

    RINGLOG_$STOP_LOGGING(&parent_index);

    ASSERT_EQ(1, unwire_count);
    ASSERT_EQ(0x00A00000u, unwired[0]);
    ASSERT_EQ(2, parent_index);
}

/* 0x00E721DC tst.b / bpl: logging off means the routine does nothing at all. */
TEST(logging_off_is_a_no_op)
{
    int16_t parent_index = 0x0F0F;

    reset();
    RINGLOG_$CTL.logging_active = 0;
    RINGLOG_$CTL.wire_count = 5;

    RINGLOG_$STOP_LOGGING(&parent_index);

    ASSERT_EQ(0, unwire_count);
    ASSERT_EQ(5, RINGLOG_$CTL.wire_count);   /* not cleared */
    ASSERT_EQ(0x0F0F, parent_index);
}

int main(void)
{
    printf("RINGLOG_$STOP_LOGGING tests\n");
    RUN_TEST(unwires_elements_zero_through_count_minus_one);
    RUN_TEST(a_full_array_stops_at_element_nine);
    RUN_TEST(zero_wire_count_unwires_nothing);
    RUN_TEST(one_wired_page_unwires_element_zero);
    RUN_TEST(logging_off_is_a_no_op);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
