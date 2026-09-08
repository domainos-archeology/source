/*
 * dir/test/test_init.c - unit tests for DIR_$INIT (0x00E3140C)
 *
 * dir/init.c is #included below over a host stand-in for the DIR module block
 * at 0x00E7DC00.  The tests cover the four per-slot stores in the loop at
 * 0x00E3144C-0x00E31494, the three chain ends broken at 0x00E3149E-0x00E314A6,
 * the two free-list heads published at 0x00E314AA/0x00E314B2, and the fact
 * that the image never writes A5+0x2040.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

/* 0xE2C058, 32 entries of 0xC, ending exactly on DIR_$LINK_BUF_MUTEX. */
ec_$eventcount_t DIR_$WAIT_ECS[32];
ec_$eventcount_t DIR_$WT_FOR_HDNL_EC;    /* 0xE2C048 */
ml_$exclusion_t  DIR_$MUTEX;             /* 0xE2C1F0 */
ml_$exclusion_t  DIR_$LINK_BUF_MUTEX;    /* 0xE2C1D8 */

static uint8_t block_store[8 + 0x2200];
#define A5_AREA (block_store + 8)

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

#define MAX_SEEN 64
static int   ec_init_calls;
static void *ec_init_args[MAX_SEEN];
void EC_$INIT(ec_$eventcount_t *ec)
{
    if (ec_init_calls < MAX_SEEN) {
        ec_init_args[ec_init_calls] = ec;
    }
    ec_init_calls++;
}

static int   excl_init_calls;
static void *excl_init_args[MAX_SEEN];
void ML_$EXCLUSION_INIT(ml_$exclusion_t *m)
{
    if (excl_init_calls < MAX_SEEN) {
        excl_init_args[excl_init_calls] = m;
    }
    excl_init_calls++;
}

static int old_init_calls;
void DIR_$OLD_INIT(void) { old_init_calls++; }

/* ------------------------------------------------------------------ */

/* Point the whole DIR module block at our own buffer (source-yv13). */
#undef DIR_$BLOCK_BASE
#define DIR_$BLOCK_BASE ((void *)A5_AREA)

#include "../init.c"

/* ------------------------------------------------------------------ */

static void reset(void)
{
    /* 0xFF everywhere so "cleared" is distinguishable from "untouched". */
    memset(block_store, 0xFF, sizeof(block_store));
    memset(DIR_$WAIT_ECS, 0, sizeof(DIR_$WAIT_ECS));
    ec_init_calls = 0;
    excl_init_calls = 0;
    old_init_calls = 0;
}

TEST(both_in_use_bitmaps_are_cleared_as_longwords)
{
    reset();
    DIR_$INIT();
    ASSERT_EQ(0u, DIR_$HANDLE_IN_USE);      /* clr.l (0x203c,A0) */
    ASSERT_EQ(0u, DIR_$LOCK_IN_USE);        /* clr.l (0x2034,A0) */
}

TEST(the_loop_runs_once_per_slot_and_numbers_both_tables)
{
    unsigned i;

    reset();
    DIR_$INIT();

    /* 32 slot event counters plus DIR_$WT_FOR_HDNL_EC at 0x00E314D6. */
    ASSERT_EQ(DIR_SLOT_COUNT + 1, ec_init_calls);
    for (i = 0; i < DIR_SLOT_COUNT; i++) {
        ASSERT_TRUE(ec_init_args[i] == (void *)&DIR_$WAIT_ECS[i]);
        ASSERT_EQ(i, DIR_$HANDLE_TAB[i].slot_index);  /* (0x18b8,A0) */
        ASSERT_EQ(i, DIR_$LOCK_TAB[i].index);         /* (0x168e,A2) */
    }
    ASSERT_TRUE(ec_init_args[DIR_SLOT_COUNT] == (void *)&DIR_$WT_FOR_HDNL_EC);
}

TEST(both_tables_are_chained_forwards_with_three_ends_broken)
{
    unsigned i;

    reset();
    DIR_$INIT();

    /* Handle slot 0 is the emergency handle and is never on the free list. */
    ASSERT_EQ(0u, DIR_$HANDLE_TAB[0].next);             /* clr.l (0x18b0,A0) */
    for (i = 1; i < DIR_SLOT_COUNT - 1; i++) {
        ASSERT_EQ(ARCH_PTR_TO_VA(&DIR_$HANDLE_TAB[i + 1]),
                  DIR_$HANDLE_TAB[i].next);
    }
    ASSERT_EQ(0u, DIR_$HANDLE_TAB[DIR_SLOT_COUNT - 1].next);       /* (0x1ff4,A0) */

    for (i = 0; i < DIR_SLOT_COUNT - 1; i++) {
        ASSERT_EQ(ARCH_PTR_TO_VA(&DIR_$LOCK_TAB[i + 1]),
                  DIR_$LOCK_TAB[i].u.next);
    }
    ASSERT_EQ(0u, DIR_$LOCK_TAB[DIR_SLOT_COUNT - 1].u.next);       /* (0x1870,A0) */
}

TEST(the_free_list_heads_point_at_the_image_addresses)
{
    reset();
    DIR_$INIT();

    /* lea (0x1680,A0),A1 -> 0x00E7F280 */
    ASSERT_EQ(ARCH_PTR_TO_VA(&DIR_$LOCK_TAB[0]), DIR_$LOCK_FREE);
    /* lea (0x18bc,A0),A1 -> 0x00E7F4BC, i.e. handle slot 1, not slot 0 */
    ASSERT_EQ(ARCH_PTR_TO_VA(&DIR_$HANDLE_TAB[1]), DIR_$HANDLE_FREE);
    ASSERT_EQ(0x1680u, DIR_LOCK_TAB_OFF);
    ASSERT_EQ(0x18BCu, DIR_HANDLE_TAB_OFF + 0x3C);
}

TEST(the_link_buffer_owner_word_is_left_alone)
{
    reset();
    /* The image has no store to A5+0x2040 anywhere in DIR_$INIT. */
    DIR_$LINK_BUF_OWNER = 0x1234;
    DIR_$INIT();
    ASSERT_EQ(0x1234, DIR_$LINK_BUF_OWNER);
}

TEST(the_two_mutexes_and_old_init_run_in_image_order)
{
    reset();
    DIR_$INIT();

    ASSERT_EQ(2, excl_init_calls);
    ASSERT_TRUE(excl_init_args[0] == (void *)&DIR_$MUTEX);          /* 0xE2C1F0 */
    ASSERT_TRUE(excl_init_args[1] == (void *)&DIR_$LINK_BUF_MUTEX); /* 0xE2C1D8 */
    ASSERT_EQ(1, old_init_calls);
}

TEST(the_chained_tables_stay_inside_the_block)
{
    reset();
    DIR_$INIT();

    /*
     * The loop links handle slot 31 to A5+0x2000 (one past the table, which
     * is DIR_$NAME_OFFSET_TABLE) and lock slot 31 to A5+0x1880 (the first
     * handle slot) before both are cleared, so nothing outside the block is
     * ever written.  Everything above A5+0x2044 is still untouched fill.
     */
    ASSERT_EQ(0xFFu, *((uint8_t *)DIR_$BLOCK_BASE + 0x2044));
    ASSERT_EQ(0xFFu, *((uint8_t *)DIR_$BLOCK_BASE + 0x2000));
    /* And the block head below A5 is untouched too. */
    ASSERT_EQ(0xFFu, block_store[0]);
}

int main(void)
{
    printf("DIR_$INIT (0x00E3140C) tests\n");

    RUN_TEST(both_in_use_bitmaps_are_cleared_as_longwords);
    RUN_TEST(the_loop_runs_once_per_slot_and_numbers_both_tables);
    RUN_TEST(both_tables_are_chained_forwards_with_three_ends_broken);
    RUN_TEST(the_free_list_heads_point_at_the_image_addresses);
    RUN_TEST(the_link_buffer_owner_word_is_left_alone);
    RUN_TEST(the_two_mutexes_and_old_init_run_in_image_order);
    RUN_TEST(the_chained_tables_stay_inside_the_block);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
