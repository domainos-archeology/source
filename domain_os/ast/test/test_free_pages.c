/*
 * ast/test/test_free_pages.c - Unit tests for AST_$FREE_PAGES (0x00E0400C)
 *
 * The test #includes ast/free_pages.c and ast/flush_installed_pages.c
 * directly and drives the real routine through mocks.  It pins:
 *
 *   - the installed-page array is 0-based and holds 32 entries; the 32nd
 *     collected page triggers a flush inside the loop;
 *   - installed pages contribute the MMAPE's disk address (& 0x3FFFFF),
 *     non-installed ones the entry's own; both mark the ASTE dirty, an
 *     empty entry does not;
 *   - every entry in the range is zeroed;
 *   - disk blocks go to BAT_$FREE(blocks, count, vol_index, 1, &status)
 *     only when vol_index != 0, 32 at a time with the lock dropped;
 *   - a BAT_$FREE failure crashes.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/free_pages.c"
#include "ast/flush_installed_pages.c"

#define TEST_N_PAGES 32
#define TEST_N_FRAMES 0x400

/* The AST_ module blocks (ast/ast.h) and the segment map (pmap/pmap.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);

/* the segment map is 1-based by segment: row n at base + n*0x80 - 0x80 */
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
uint16_t        PROC1_$CURRENT;

static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static int wait_calls;
static uint32_t *wait_entry_to_clear;
void ast_$wait_for_page_transition(void)
{
    wait_calls++;
    if (wait_entry_to_clear != NULL) {
        *wait_entry_to_clear &= ~SEGMAP_IN_TRANS;
    }
}

#define MAX_REC 8
static int      remove_calls;
static uint16_t remove_counts[MAX_REC];
static uint32_t remove_first[MAX_REC];
void (MMU_$REMOVE_LIST)(uint32_t *ppn_array, uint32_t count_slot)
{
    uint16_t count = (uint16_t)ARCH_PASCAL_SLOT_WORD(count_slot); (void)count;
    if (remove_calls < MAX_REC) {
        remove_counts[remove_calls] = count;
        remove_first[remove_calls] = ppn_array[0];
    }
    remove_calls++;
}

static int free_pages_calls;
void MMAP_$FREE_PAGES(uint16_t pid, uint32_t *vpn_array, uint16_t count)
{
    (void)pid;
    (void)vpn_array; (void)count;
    free_pages_calls++;
}

static int       bat_calls;
static int16_t   bat_counts[MAX_REC];
static int16_t   bat_vols[MAX_REC];
static int16_t   bat_reserved[MAX_REC];
static uint32_t  bat_first[MAX_REC];
static uint32_t  bat_last[MAX_REC];
static int       bat_lock_depth[MAX_REC];
static status_$t bat_status;
void BAT_$FREE(uint32_t *blocks, int16_t count, int16_t vol_idx,
               int16_t reserved, status_$t *status)
{
    if (bat_calls < MAX_REC) {
        bat_counts[bat_calls] = count;
        bat_vols[bat_calls] = vol_idx;
        bat_reserved[bat_calls] = reserved;
        bat_first[bat_calls] = blocks[0];
        bat_last[bat_calls] = blocks[count - 1];
        bat_lock_depth[bat_calls] = lock_calls - unlock_calls;
    }
    bat_calls++;
    *status = bat_status;
}

static int crash_calls;
void CRASH_SYSTEM(const status_$t *status_p) { (void)status_p; crash_calls++; }

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    lock_calls = unlock_calls = 0;
    wait_calls = 0; wait_entry_to_clear = NULL;
    remove_calls = 0; memset(remove_counts, 0, sizeof(remove_counts));
    free_pages_calls = 0;
    bat_calls = 0; bat_status = status_$ok;
    crash_calls = 0;
    PROC1_$CURRENT = 2;
}

/* segment 1's row, PMAP_$SEGMAP.row[0] */
static uint32_t *row1(void) { return (uint32_t *)PMAP_SEGMAP_ROW(1); }

TEST(mixed_entries_dirty_and_blocks)
{
    aste_t aste;
    uint32_t *row = row1();

    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 1;
    aste.page_count = 5;

    row[0] = SEGMAP_VALID | 0x210;              /* installed, ppn 0x210 */
    MMAPE_FOR_VPN(0x210)->disk_addr = 0xC0001234;  /* & 0x3FFFFF = 0x1234 */
    row[1] = 0x00005678;                        /* on disk */
    row[2] = 0;                                 /* empty */
    row[3] = SEGMAP_VALID | 0x211;              /* installed, no disk addr */
    MMAPE_FOR_VPN(0x211)->disk_addr = 0;

    AST_$FREE_PAGES(&aste, 0, 3, 4);

    ASSERT_EQ(ASTE_FLAG_DIRTY, aste.flags);
    ASSERT_EQ(0, row[0]); ASSERT_EQ(0, row[1]); ASSERT_EQ(0, row[2]); ASSERT_EQ(0, row[3]);
    /* one final flush of two pages */
    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(2, remove_counts[0]);
    ASSERT_EQ(0x210, remove_first[0]);
    ASSERT_EQ(1, free_pages_calls);
    ASSERT_EQ(3, aste.page_count);              /* 5 - 2 */
    /* two blocks freed after the unlock */
    ASSERT_EQ(1, bat_calls);
    ASSERT_EQ(2, bat_counts[0]);
    ASSERT_EQ(4, bat_vols[0]);
    ASSERT_EQ(1, bat_reserved[0]);
    ASSERT_EQ(0x1234, bat_first[0]);
    ASSERT_EQ(0x5678, bat_last[0]);
    ASSERT_EQ(0, bat_lock_depth[0]);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, crash_calls);
}

TEST(vol_index_zero_keeps_blocks)
{
    aste_t aste;
    uint32_t *row = row1();

    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 1;
    row[5] = 0x00000777;

    AST_$FREE_PAGES(&aste, 5, 5, 0);

    ASSERT_EQ(0, bat_calls);
    ASSERT_EQ(0, row[5]);
    ASSERT_EQ(ASTE_FLAG_DIRTY, aste.flags);
    ASSERT_EQ(0, remove_calls);
}

TEST(thirty_two_installed_pages_flush_in_loop)
{
    aste_t aste;
    uint32_t *row = row1();
    int i;

    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 1;
    aste.page_count = 32;
    for (i = 0; i < 32; i++) {
        row[i] = SEGMAP_VALID | (0x300 + i);
    }

    AST_$FREE_PAGES(&aste, 0, 31, 0);

    /* the 32nd page flushed inside the loop; nothing left at the end */
    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(32, remove_counts[0]);
    ASSERT_EQ(0x300, remove_first[0]);
    ASSERT_EQ(0, aste.page_count);
    for (i = 0; i < 32; i++) {
        ASSERT_EQ(0, row[i]);
    }
}

TEST(thirty_two_blocks_free_mid_loop)
{
    aste_t aste;
    uint32_t *row = row1();
    int i;

    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 2;
    row = (uint32_t *)PMAP_SEGMAP_ROW(2);
    for (i = 0; i < 32; i++) {
        row[i] = 0x1000 + i;
    }
    /* a 33rd page from... only 32 in a segment; use a second BAT call
     * through the tail instead: 32 blocks -> mid-loop call, tail empty */

    AST_$FREE_PAGES(&aste, 0, 31, 3);

    ASSERT_EQ(1, bat_calls);
    ASSERT_EQ(32, bat_counts[0]);
    ASSERT_EQ(0x1000, bat_first[0]);
    ASSERT_EQ(0x101F, bat_last[0]);
    ASSERT_EQ(0, bat_lock_depth[0]);            /* unlocked around the call */
    ASSERT_EQ(2, lock_calls);                   /* re-locked afterwards */
    ASSERT_EQ(2, unlock_calls);
}

TEST(in_transition_flushes_then_waits)
{
    aste_t aste;
    uint32_t *row = row1();

    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 1;
    aste.page_count = 1;
    row[0] = SEGMAP_VALID | 0x220;
    row[1] = SEGMAP_IN_TRANS | 0x00000055;
    wait_entry_to_clear = &row[1];

    AST_$FREE_PAGES(&aste, 0, 1, 0);

    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(1, remove_calls);                 /* flushed before waiting */
    ASSERT_EQ(1, remove_counts[0]);
    ASSERT_EQ(0, row[1]);
}

TEST(empty_range_only_locks)
{
    aste_t aste;

    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 1;

    AST_$FREE_PAGES(&aste, 4, 3, 1);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(0, bat_calls);
}

TEST(bat_failure_crashes)
{
    aste_t aste;
    uint32_t *row = row1();

    memset(&aste, 0, sizeof(aste));
    aste.seg_index = 1;
    row[0] = 0x00000042;
    bat_status = 0x00010001;

    AST_$FREE_PAGES(&aste, 0, 0, 1);

    ASSERT_EQ(1, bat_calls);
    ASSERT_EQ(1, crash_calls);
}

int main(void)
{
    printf("test_free_pages (AST_$FREE_PAGES 0x00E0400C)\n");

    RUN_TEST(mixed_entries_dirty_and_blocks);
    RUN_TEST(vol_index_zero_keeps_blocks);
    RUN_TEST(thirty_two_installed_pages_flush_in_loop);
    RUN_TEST(thirty_two_blocks_free_mid_loop);
    RUN_TEST(in_transition_flushes_then_waits);
    RUN_TEST(empty_range_only_locks);
    RUN_TEST(bat_failure_crashes);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
