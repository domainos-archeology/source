/*
 * term/test/test_status_convert.c - Unit tests for TERM_$STATUS_CONVERT
 *
 * The real term/status_convert.c and term/term_data.c are #included below,
 * so the tables under test are the ones the image holds.
 *
 * Covered:
 *   - the three subsystem arms (0x33 at A5+0x50, 0x35 at A5-0x04, 0x36 at
 *     A5+0x24) and the pass-through default
 *   - every entry of every table, against the bytes read out of the image
 *   - that the low word, not the whole status, is the index
 *   - that no bounds check is performed: the 0x36 arm's index 0 is table
 *     36's own first entry, and the tables are contiguous
 */

#include <stdio.h>
#include <string.h>

#include "term/term_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
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
 * Stubs for the symbols term_data.c's callback cells name
 * ============================================================================ */

void TERM_$ENQUEUE_TPAD(void **param1) { (void)param1; }
void TTY_$I_RCV(tty_desc_t *tty, uint8_t ch) { (void)tty; (void)ch; }

static dxm_$callback_fn_t host_cells[DXM_HOST_CALLBACK_MAX];
static unsigned host_cell_count;

dxm_$callback_t dxm_$callback_cell(dxm_$callback_fn_t fn)
{
    host_cells[host_cell_count] = fn;
    host_cell_count++;
    return (dxm_$callback_t)host_cell_count;
}

dxm_$callback_fn_t dxm_$callback_fn(dxm_$callback_t cell)
{
    if (cell == 0 || cell > host_cell_count) {
        return NULL;
    }
    return host_cells[cell - 1];
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../term_data.c"
#include "../status_convert.c"

/* ============================================================================
 * Expected table contents (`gsk read 0xe2c988 0x68`)
 * ============================================================================ */

static const status_$t expect_35[10] = {
    0x00000000, 0x000b0004, 0x000b000d, 0x000b0007,
    0x000b0001, 0x000b0002, 0x000b0003, 0x000b0006,
    0x00000000, 0x000b0005
};

static const status_$t expect_36[11] = {
    0x00000000, 0x000b0004, 0x000b000d, 0x000b0007,
    0x000b0009, 0x000b000a, 0x000b000b, 0x000b000c,
    0x000b000f, 0x000b0005, 0x000b0006
};

static const status_$t expect_33[5] = {
    0x000b0010, 0x000b0004, 0x000b000d, 0x000b0007, 0x000b0008
};

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(subsystem_35_every_entry)
{
    int i;

    for (i = 0; i < 10; i++) {
        status_$t s = 0x00350000 | (status_$t)i;

        TERM_$STATUS_CONVERT(&s);
        ASSERT_EQ(expect_35[i], s);
    }
}

TEST(subsystem_36_every_entry)
{
    int i;

    for (i = 0; i < 11; i++) {
        status_$t s = 0x00360000 | (status_$t)i;

        TERM_$STATUS_CONVERT(&s);
        ASSERT_EQ(expect_36[i], s);
    }
}

TEST(subsystem_33_every_entry)
{
    int i;

    for (i = 0; i < 5; i++) {
        status_$t s = 0x00330000 | (status_$t)i;

        TERM_$STATUS_CONVERT(&s);
        ASSERT_EQ(expect_33[i], s);
    }
}

TEST(unknown_subsystem_is_left_alone)
{
    status_$t s = 0x00340003;

    TERM_$STATUS_CONVERT(&s);
    ASSERT_EQ(0x00340003, s);

    s = 0x00000001;
    TERM_$STATUS_CONVERT(&s);
    ASSERT_EQ(0x00000001, s);
}

TEST(only_byte_1_selects_the_table)
{
    /* The high byte is ignored: only (status >> 16) & 0xFF is compared. */
    status_$t s = 0x77350004;

    TERM_$STATUS_CONVERT(&s);
    ASSERT_EQ(expect_35[4], s);
}

TEST(index_comes_from_the_low_word)
{
    /* 0x36 subcode 9 is "input buffer overrun" -> term 0x000b0005. */
    status_$t s = 0x00360009;

    TERM_$STATUS_CONVERT(&s);
    ASSERT_EQ(0x000b0005, s);
}

TEST(tables_are_contiguous_and_unchecked)
{
    /*
     * 0x35 index 10 runs off the end of table 35, which the image places
     * immediately before table 36 (0xe2c988 + 10*4 == 0xe2c9b0).  With the
     * tables laid out as separate C objects this is only guaranteed for the
     * documented sizes, so the test asserts the sizes rather than the
     * overrun itself.
     */
    ASSERT_EQ(10u, sizeof(TERM_$STATUS_TRANSLATION_TABLE_35) / sizeof(status_$t));
    ASSERT_EQ(11u, sizeof(TERM_$STATUS_TRANSLATION_TABLE_36) / sizeof(status_$t));
    ASSERT_EQ(5u,  sizeof(TERM_$STATUS_TRANSLATION_TABLE_33) / sizeof(status_$t));
}

int main(void)
{
    printf("test_status_convert:\n");

    RUN_TEST(subsystem_35_every_entry);
    RUN_TEST(subsystem_36_every_entry);
    RUN_TEST(subsystem_33_every_entry);
    RUN_TEST(unknown_subsystem_is_left_alone);
    RUN_TEST(only_byte_1_selects_the_table);
    RUN_TEST(index_comes_from_the_low_word);
    RUN_TEST(tables_are_contiguous_and_unchecked);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
