/*
 * file/test/test_lock_init.c - unit tests for FILE_$LOCK_INIT (0x00E32744)
 *
 * The real file/lock_init.c is #included below together with the real
 * file/file_data.c, so the tables under test are the ones the subsystem
 * actually uses.  On a host build FILE_$LOT_BASE / FILE_$PROC_LOT_BASE /
 * FILE_$PROC_LOT_CNT_BASE resolve to those C globals instead of the m68k
 * absolute addresses, so FILE_$LOCK_INIT can simply be called.
 *
 * Each assertion below names the instruction it is checking (bead source-0sgi).
 */

#include <stdio.h>
#include <string.h>

#include "file/file_internal.h"
#include "ec/ec.h"
#include "rem_file/rem_file.h"
#include "uid/uid.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-28s ... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Mocked globals and callees
 * ============================================================================ */

uid_t    UID_$NIL  = { 0x11223344u, 0xAABBCCDDu };
uint32_t NODE_$ME  = 0x000ABCDEu;

static int      mock_ec_init_called;
static void    *mock_ec_init_arg;
static int      mock_uid_gen_called;
static void    *mock_uid_gen_arg;
static int      mock_unlock_all_called;

void EC_$INIT(ec_$eventcount_t *ec)
{
    mock_ec_init_called++;
    mock_ec_init_arg = ec;
}

void UID_$GEN(uid_t *uid_ret)
{
    mock_uid_gen_called++;
    mock_uid_gen_arg = uid_ret;
    uid_ret->high = 0xFEEDFACEu;
    uid_ret->low  = 0x0BADF00Du;
}

void REM_FILE_$UNLOCK_ALL(void)
{
    mock_unlock_all_called++;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../file_data.c"
#include "../lock_init.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static void poison_world(void)
{
    memset(FILE_$LOCK_ENTRIES, 0xA5, sizeof(FILE_$LOCK_ENTRIES));
    memset(FILE_$LOCK_TABLE, 0xA5, sizeof(FILE_$LOCK_TABLE));
    memset(FILE_$LOCK_TABLE2, 0xA5, sizeof(FILE_$LOCK_TABLE2));
    memset(&FILE_$LOCK_CONTROL, 0xA5, sizeof(FILE_$LOCK_CONTROL));
    memset(FILE_$LOT_HASHTAB, 0xA5, sizeof(FILE_$LOT_HASHTAB));
    FILE_$LOT_E9F9C4 = 0xA5A5;
    FILE_$LOT_FULL   = (int8_t)0xA5;

    mock_ec_init_called    = 0;
    mock_ec_init_arg       = NULL;
    mock_uid_gen_called    = 0;
    mock_uid_gen_arg       = NULL;
    mock_unlock_all_called = 0;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/*
 * 0x00E32760-0x00E32780: 58 rows, 150 words each, cleared from the ROW BASE
 * (0xE9F9CA + 2 = 0xE9F9CC, xref 0x00E3276C -> 0xE9F9CC).  Slot 1 is the very
 * first word cleared, so nothing in a row survives.
 */
TEST(clears_every_slot_of_every_proc_row)
{
    int asid, slot;

    poison_world();
    FILE_$LOCK_INIT();

    for (asid = 0; asid < FILE_LOCK_TABLE_ENTRIES; asid++) {
        for (slot = 1; slot <= 150; slot++) {
            ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(asid, slot));
        }
    }
}

/* 0x00E32776: clr.w (0x1d98,A1) - 0xEA3DC4 + asid*2, one per row. */
TEST(clears_every_proc_row_count)
{
    int asid;

    poison_world();
    FILE_$LOCK_INIT();

    for (asid = 0; asid < FILE_LOCK_TABLE_ENTRIES; asid++) {
        ASSERT_EQ(0, FILE_$PROC_LOT_COUNT(asid));
    }
}

/*
 * 0x00E32784-0x00E327A8: the free list is built over the 1-BASED entries
 * 1..1792, each entry's `next` (+0x14) getting index+1 and its refcount
 * (+0x18) getting zero.  Entry 1 is the array base 0xE935CC.
 */
TEST(builds_one_based_free_list)
{
    int index;

    poison_world();
    FILE_$LOCK_INIT();

    /* Entry 1 is the first element of the array, not element [1]. */
    ASSERT_EQ((void *)&FILE_$LOCK_ENTRIES[0], (void *)FILE_$LOT_ENTRY(1));

    for (index = 1; index <= FILE_LOCK_ENTRY_COUNT; index++) {
        ASSERT_EQ(index + 1, FILE_$LOT_ENTRY(index)->next);
        ASSERT_EQ(0,         FILE_$LOT_ENTRY(index)->refcount);
    }

    /* The last link points one past the table (0x00E3279E writes 1793 into
     * entry 1792's `next`), and the loop stops there: the 1793rd slot is left
     * exactly as it was.  On the machine it is BSS, so its zero `next` is what
     * stops FILE_$PRIV_LOCK_$ALLOC_ENTRY (0x00E5EBB4). */
    ASSERT_EQ(FILE_LOCK_ENTRY_COUNT + 1,
              FILE_$LOT_ENTRY(FILE_LOCK_ENTRY_COUNT)->next);
    ASSERT_EQ(0xA5A5, FILE_$LOT_ENTRY(FILE_LOCK_ENTRY_COUNT + 1)->next);
}

/*
 * From the power-up (zeroed) state the machine actually boots in, the
 * never-written 1793rd slot terminates the free list.
 */
TEST(sentinel_slot_terminates_free_list_from_zeroed_memory)
{
    poison_world();
    memset(FILE_$LOCK_ENTRIES, 0, sizeof(FILE_$LOCK_ENTRIES));
    FILE_$LOCK_INIT();

    ASSERT_EQ(0, FILE_$LOT_ENTRY(FILE_LOCK_ENTRY_COUNT + 1)->next);
}

/*
 * The loop writes ONLY +0x14 and +0x18; the poison in every other field of
 * every entry must survive.  This is the assertion the old, stale
 * file_lock_entry_t model could not express - it called +0x18 `flags` and had
 * no refcount at all.
 */
TEST(free_list_loop_touches_only_next_and_refcount)
{
    int index;

    poison_world();
    FILE_$LOCK_INIT();

    for (index = 1; index <= FILE_LOCK_ENTRY_COUNT; index++) {
        file_lock_entry_detail_t *e = FILE_$LOT_ENTRY(index);
        ASSERT_EQ(0xA5A5A5A5u, e->context);
        ASSERT_EQ(0xA5A5A5A5u, e->node_low);
        ASSERT_EQ(0xA5A5A5A5u, e->node_high);
        ASSERT_EQ(0xA5A5A5A5u, e->uid_high);
        ASSERT_EQ(0xA5A5A5A5u, e->uid_low);
        ASSERT_EQ(0xA5A5,      e->sequence);
        ASSERT_EQ(0xA5,        e->flags1);
        ASSERT_EQ(0xA5,        e->rights);
        ASSERT_EQ(0xA5,        e->flags2);
    }
}

/* 0x00E327AC: clr.w (0x00e9f9c4).l */
TEST(clears_word_at_e9f9c4)
{
    poison_world();
    FILE_$LOCK_INIT();
    ASSERT_EQ(0, FILE_$LOT_E9F9C4);
}

/* 0x00E327BE-0x00E327CA: 0xFB = 251 words from control+0xC8. */
TEST(clears_251_words_of_lock_map)
{
    int i;

    poison_world();
    FILE_$LOCK_INIT();

    for (i = 0; i < 251; i++) {
        ASSERT_EQ(0, FILE_$LOCK_CONTROL.lock_map[i]);
    }
    /* control+0x2BE..0x2CB is never touched (0xA5 survives). */
    ASSERT_EQ(0xA5, FILE_$LOCK_CONTROL.reserved_2be[0]);
    ASSERT_EQ(0xA5, FILE_$LOCK_CONTROL.reserved_2be[13]);
}

/*
 * 0x00E327B8 and 0x00E327E2 both write 1 to control+0x2CE; 0x00E327E8 writes 1
 * to control+0x2CC; 0x00E32820 clears control+0x2D0.
 */
TEST(sets_control_scalars)
{
    poison_world();
    FILE_$LOCK_INIT();

    ASSERT_EQ(1, FILE_$LOCK_CONTROL.lot_free);
    ASSERT_EQ(1, FILE_$LOT_FREE);
    ASSERT_EQ(1, FILE_$LOCK_CONTROL.flag_2cc);
    ASSERT_EQ(1, FILE_$LOT_HIGH);
    ASSERT_EQ(0, FILE_$LOCK_CONTROL.flag_2d0);
    ASSERT_EQ(0, FILE_$LOT_FULL);
}

/*
 * 0x00E327FA-0x00E3281C: base_uid = UID_$NIL with the low longword's bottom
 * 20 bits replaced by NODE_$ME (andi.l #-0x100000 then or.l).
 */
TEST(derives_base_uid_from_uid_nil_and_node_me)
{
    poison_world();
    FILE_$LOCK_INIT();

    ASSERT_EQ(UID_$NIL.high, FILE_$LOCK_CONTROL.base_uid.high);
    ASSERT_EQ((UID_$NIL.low & 0xFFF00000u) | NODE_$ME,
              FILE_$LOCK_CONTROL.base_uid.low);
    /* 0xAABBCCDD & 0xFFF00000 | 0x000ABCDE */
    ASSERT_EQ(0xAABABCDEu, FILE_$LOCK_CONTROL.base_uid.low);
}

/* 0x00E327CE EC_$INIT, 0x00E327F2 UID_$GEN(control+0xC0), 0x00E32824. */
TEST(calls_out_once_each)
{
    poison_world();
    FILE_$LOCK_INIT();

    ASSERT_EQ(1, mock_ec_init_called);
    ASSERT_EQ((void *)&FILE_$UID_LOCK_EC, mock_ec_init_arg);
    ASSERT_EQ(1, mock_uid_gen_called);
    ASSERT_EQ((void *)&FILE_$LOCK_CONTROL.generated_uid, mock_uid_gen_arg);
    ASSERT_EQ(0xFEEDFACEu, FILE_$LOCK_CONTROL.generated_uid.high);
    ASSERT_EQ(1, mock_unlock_all_called);
}

/* Running it twice must be idempotent - the original has no first-time guard. */
TEST(is_idempotent)
{
    poison_world();
    FILE_$LOCK_INIT();
    FILE_$LOCK_INIT();

    ASSERT_EQ(2, FILE_$LOT_ENTRY(1)->next);
    ASSERT_EQ(FILE_LOCK_ENTRY_COUNT + 1,
              FILE_$LOT_ENTRY(FILE_LOCK_ENTRY_COUNT)->next);
    ASSERT_EQ(1, FILE_$LOT_FREE);
    ASSERT_EQ(2, mock_unlock_all_called);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    printf("FILE_$LOCK_INIT tests\n");

    RUN_TEST(clears_every_slot_of_every_proc_row);
    RUN_TEST(clears_every_proc_row_count);
    RUN_TEST(builds_one_based_free_list);
    RUN_TEST(sentinel_slot_terminates_free_list_from_zeroed_memory);
    RUN_TEST(free_list_loop_touches_only_next_and_refcount);
    RUN_TEST(clears_word_at_e9f9c4);
    RUN_TEST(clears_251_words_of_lock_map);
    RUN_TEST(sets_control_scalars);
    RUN_TEST(derives_base_uid_from_uid_nil_and_node_me);
    RUN_TEST(calls_out_once_each);
    RUN_TEST(is_idempotent);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
