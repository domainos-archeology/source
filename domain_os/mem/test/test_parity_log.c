/*
 * Test cases for MEM_$PARITY_LOG (0x00E0ADB0) and the MEM_ A5 block.
 *
 * This test builds the real implementation - it includes mem/parity_log.c and
 * mem/mem_data.c directly, so the layout it pins is the one the kernel build
 * uses.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "mem/mem_internal.h"

#include "../mem_data.c"
#include "../parity_log.c"

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT_EQ(actual, expected, what)                                      \
    do {                                                                       \
        unsigned long _a = (unsigned long)(actual);                            \
        unsigned long _e = (unsigned long)(expected);                          \
        if (_a != _e) {                                                        \
            printf("    FAIL: %s: got 0x%lx, expected 0x%lx\n", (what), _a, _e);\
            tests_failed++;                                                    \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                       \
        int _before = tests_failed;                                            \
        tests_run++;                                                           \
        fn();                                                                  \
        printf("  %-34s %s\n", #fn, tests_failed == _before ? "PASS" : "FAIL"); \
    } while (0)

/* Clear the block the way the image ships it (all zero but the two words). */
static void reset_state(void)
{
    memset(&MEM_DATA, 0, sizeof(MEM_DATA));
    MEM_$MEM_REC.w_00 = 0x0002;
    MEM_$MEM_REC.w_02 = 0x0002;
}

/*
 * The layout is what the SAU2 image proves; pin every offset the accessing
 * instructions name, in bytes from the block base 0x00E22930.
 */
static void test_block_layout(void)
{
#define BLOCK_OFF(field) ((unsigned long)__builtin_offsetof(mem_data_t, field))

    ASSERT_EQ(sizeof(mem_data_t), 0x5C, "MEM_ segment size");
    ASSERT_EQ(BLOCK_OFF(size), 0x00, "MEM_$SIZE @ 0xE22930");
    ASSERT_EQ(BLOCK_OFF(rec), 0x04, "MEM_$MEM_REC @ 0xE22934");
    ASSERT_EQ(sizeof(mem_$mem_rec_t), 0x56, "MEM_$MEM_REC size (ASKNODE copy)");
    ASSERT_EQ(BLOCK_OFF(rec.board_errors), 0x08, "board_errors base");
    ASSERT_EQ(BLOCK_OFF(rec.board_errors[1]), 0x0A, "board 1 count");
    ASSERT_EQ(BLOCK_OFF(rec.board_errors[2]), 0x0C, "board 2 count");
    ASSERT_EQ(BLOCK_OFF(rec.w_0a), 0x0E, "w_0a @ 0xE2293E");
    ASSERT_EQ(BLOCK_OFF(rec.w_0c), 0x10, "w_0c @ 0xE22940");
    ASSERT_EQ(BLOCK_OFF(rec.page_errors), 0x12, "page_errors @ 0xE22942");
    ASSERT_EQ(BLOCK_OFF(rec.page_errors[0].count), 0x16, "records[0].count");
    ASSERT_EQ(BLOCK_OFF(rec.page_errors[3]), 0x48, "page_errors[3]");
    ASSERT_EQ(BLOCK_OFF(w_5a), 0x5A, "segment tail word");
    ASSERT_EQ(sizeof(mem_$page_error_t), 0x12, "page error record stride");

    /* The 1-based addressing the original uses: A5 + 18*i names record i-1. */
    ASSERT_EQ(BLOCK_OFF(rec.page_errors[0]), 0x12 * 1, "A5 + 18*1");
    ASSERT_EQ(BLOCK_OFF(rec.page_errors[1]), 0x12 * 2, "A5 + 18*2");
    ASSERT_EQ(BLOCK_OFF(rec.page_errors[2]), 0x12 * 3, "A5 + 18*3");
    ASSERT_EQ(BLOCK_OFF(rec.page_errors[3]), 0x12 * 4, "A5 + 18*4");

#undef BLOCK_OFF
}

/* Board 1 is < 3MB, board 2 is >= 3MB, and only those two slots move. */
static void test_board_counting(void)
{
    reset_state();

    MEM_$PARITY_LOG(0x100000);
    ASSERT_EQ(MEM_$BOARD_ERRORS[1], 1, "board 1 after 0x100000");
    ASSERT_EQ(MEM_$BOARD_ERRORS[2], 0, "board 2 untouched");

    MEM_$PARITY_LOG(0x2FFFFF);
    ASSERT_EQ(MEM_$BOARD_ERRORS[1], 2, "board 1 after 0x2FFFFF");

    MEM_$PARITY_LOG(0x300000);
    ASSERT_EQ(MEM_$BOARD_ERRORS[1], 2, "board 1 unchanged at boundary");
    ASSERT_EQ(MEM_$BOARD_ERRORS[2], 1, "board 2 at 0x300000");

    MEM_$PARITY_LOG(0x400000);
    ASSERT_EQ(MEM_$BOARD_ERRORS[2], 2, "board 2 after 0x400000");

    ASSERT_EQ(MEM_$BOARD_ERRORS[0], 0, "bias slot at 0xE22938 never written");
}

/* The first error fills record 0 with the full longword address, count 1. */
static void test_first_error_stores(void)
{
    reset_state();

    MEM_$PARITY_LOG(0x00123456);
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].phys_addr, 0x00123456, "records[0].phys_addr");
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].count, 1, "records[0].count");
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].count, 0, "records[1] untouched");
    for (int i = 0; i < 12; i++) {
        ASSERT_EQ(MEM_$PAGE_ERRORS[0].reserved[i], 0, "reserved bytes untouched");
    }
}

/*
 * The page id is bits 21..16 (byte 1 of the longword, masked 0x3F), NOT bits
 * 8..13: 0x100400 and 0x100480 are the same record because their byte 1 is
 * equal, and so are 0x100400 and 0x1FFFFF.
 */
static void test_page_id_is_bits_21_16(void)
{
    reset_state();

    MEM_$PARITY_LOG(0x00100400);
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].count, 1, "first error");

    /* Same byte 1 (0x10 & 0x3F) -> same record, count increments in place. */
    MEM_$PARITY_LOG(0x0010FFFF);
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].count, 2, "same page id increments");
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].phys_addr, 0x00100400, "address not rewritten");
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].count, 0, "no second record created");

    /* Differing only below bit 16 is still the same record. */
    MEM_$PARITY_LOG(0x00100000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].count, 3, "low bits ignored");

    /* Byte 1 differs -> a new record. */
    MEM_$PARITY_LOG(0x00110000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].count, 1, "new page id takes records[1]");
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].phys_addr, 0x00110000, "records[1].phys_addr");

    /* The mask is 0x3F, so bits 23..22 are not part of the id: 0x00 and 0x40
     * in byte 1 collide. */
    reset_state();
    MEM_$PARITY_LOG(0x00050000);
    MEM_$PARITY_LOG(0x00450000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].count, 2, "0x3F mask ignores bits 23..22");
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].count, 0, "no second record");
}

/* Four distinct page ids fill the table front to back. */
static void test_table_fills_in_order(void)
{
    reset_state();

    MEM_$PARITY_LOG(0x00010000);
    MEM_$PARITY_LOG(0x00020000);
    MEM_$PARITY_LOG(0x00030000);
    MEM_$PARITY_LOG(0x00040000);

    ASSERT_EQ(MEM_$PAGE_ERRORS[0].phys_addr, 0x00010000, "records[0]");
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].phys_addr, 0x00020000, "records[1]");
    ASSERT_EQ(MEM_$PAGE_ERRORS[2].phys_addr, 0x00030000, "records[2]");
    ASSERT_EQ(MEM_$PAGE_ERRORS[3].phys_addr, 0x00040000, "records[3]");
    for (int i = 0; i < MEM_PAGE_ERROR_RECORDS; i++) {
        ASSERT_EQ(MEM_$PAGE_ERRORS[i].count, 1, "each record counted once");
    }
}

/*
 * With the table full, a new page id replaces the lowest count.  The scan
 * starts at records[0] as the running minimum and only compares records 1..3,
 * so ties keep the earliest record.
 */
static void test_replaces_lowest_count(void)
{
    reset_state();

    MEM_$PARITY_LOG(0x00010000);
    MEM_$PARITY_LOG(0x00020000);
    MEM_$PARITY_LOG(0x00030000);
    MEM_$PARITY_LOG(0x00040000);

    /* Make records 0, 1 and 3 heavier than record 2. */
    MEM_$PARITY_LOG(0x00010000);
    MEM_$PARITY_LOG(0x00020000);
    MEM_$PARITY_LOG(0x00040000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[2].count, 1, "records[2] is the minimum");

    MEM_$PARITY_LOG(0x00050000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[2].phys_addr, 0x00050000, "records[2] replaced");
    ASSERT_EQ(MEM_$PAGE_ERRORS[2].count, 1, "replacement count is 1");
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].count, 2, "records[0] survives");
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].count, 2, "records[1] survives");
    ASSERT_EQ(MEM_$PAGE_ERRORS[3].count, 2, "records[3] survives");

    /* All four equal -> the tie goes to records[0]. */
    reset_state();
    MEM_$PARITY_LOG(0x00010000);
    MEM_$PARITY_LOG(0x00020000);
    MEM_$PARITY_LOG(0x00030000);
    MEM_$PARITY_LOG(0x00040000);
    MEM_$PARITY_LOG(0x00050000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].phys_addr, 0x00050000, "tie replaces records[0]");
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].phys_addr, 0x00020000, "records[1] kept");
}

/*
 * A zero count ends the forward scan, so a hole left behind a live record is
 * refilled by the minimum scan rather than by the scan that found it.
 */
static void test_zero_count_ends_scan(void)
{
    reset_state();

    MEM_$PARITY_LOG(0x00010000);
    MEM_$PARITY_LOG(0x00020000);

    /* Punch out records[0]; the forward scan now stops immediately. */
    MEM_$PAGE_ERRORS[0].count = 0;

    MEM_$PARITY_LOG(0x00020000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].count, 1, "records[1] not incremented");
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].phys_addr, 0x00020000, "records[0] refilled");
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].count, 1, "records[0] count set to 1");
}

/*
 * The minimum comparison widens the candidate as unsigned and the running
 * minimum as signed, so a count with bit 15 set (only reachable by wrap) is
 * seen as a large positive candidate and as a negative running minimum.
 */
static void test_signedness_of_minimum_scan(void)
{
    reset_state();

    MEM_$PARITY_LOG(0x00010000);
    MEM_$PARITY_LOG(0x00020000);
    MEM_$PARITY_LOG(0x00030000);
    MEM_$PARITY_LOG(0x00040000);

    /* records[0] starts the scan sign-extended: 0x8000 reads as -32768, and
     * no unsigned candidate can beat it, so records[0] is replaced. */
    MEM_$PAGE_ERRORS[0].count = 0x8000;
    MEM_$PARITY_LOG(0x00050000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[0].phys_addr, 0x00050000, "negative min wins");

    /* A candidate with bit 15 set is zero-extended, so it never wins. */
    reset_state();
    MEM_$PARITY_LOG(0x00010000);
    MEM_$PARITY_LOG(0x00020000);
    MEM_$PARITY_LOG(0x00030000);
    MEM_$PARITY_LOG(0x00040000);
    MEM_$PAGE_ERRORS[0].count = 5;
    MEM_$PAGE_ERRORS[2].count = 0x8000;
    MEM_$PARITY_LOG(0x00050000);
    ASSERT_EQ(MEM_$PAGE_ERRORS[1].phys_addr, 0x00050000, "0x8000 candidate ignored");
    ASSERT_EQ(MEM_$PAGE_ERRORS[2].count, 0x8000, "records[2] untouched");
}

int main(void)
{
    printf("MEM_$PARITY_LOG (0x00E0ADB0)\n");

    RUN_TEST(test_block_layout);
    RUN_TEST(test_board_counting);
    RUN_TEST(test_first_error_stores);
    RUN_TEST(test_page_id_is_bits_21_16);
    RUN_TEST(test_table_fills_in_order);
    RUN_TEST(test_replaces_lowest_count);
    RUN_TEST(test_zero_count_ends_scan);
    RUN_TEST(test_signedness_of_minimum_scan);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
