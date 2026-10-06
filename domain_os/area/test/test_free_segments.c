/*
 * area/test/test_free_segments.c - unit tests for area_$free_segments
 * (0x00E085A6) and its nested procedure (0x00E082A8)
 *
 * The area table, ASTE table and overflow tables are host objects;
 * area_$get_aste, the AST/DBUF/BAT callees, area_$free_seg_table and the
 * ML lock are mocked.
 */

#include <stdio.h>
#include <stdint.h>
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

#include "area/area_internal.h"
#include "ast/ast.h"
#include "bat/bat.h"
#include "dbuf/dbuf.h"
#include "misc/crash_system.h"

#define TABLE_N 8
static area_$entry_t mock_area_table[TABLE_N];
#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)
#undef AREA_ENTRY_SIZE
#define AREA_ENTRY_SIZE ((int)sizeof(area_$entry_t))

area_$globals_t AREA_$GLOBALS;
status_$t Area_Internal_Error = 0x0032000A;
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

/* ---- mocks ----------------------------------------------------------- */

#define LOGN 64
static int locks, unlocks, crashes, get_calls, frees_p, frees_a, invals, bats, tfrees;
static uint16_t get_seg[LOGN];
static int8_t get_create[LOGN];
static int16_t fp_start[LOGN], fp_end[LOGN], fp_vol;
static aste_t *fa[LOGN];
static uint32_t inval_blk[LOGN];
static uint32_t bat_blk[LOGN];
static int16_t bat_count, bat_vol;
static area_$seg_table_t *tfree_tbl, *tfree_prev;
static int16_t tfree_asid;
static status_$t get_status;
static aste_t *get_result;

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlocks++; }
void CRASH_SYSTEM(const status_$t *s) { (void)s; crashes++; }
void area_$wait_pite_in_trans(void) { }
short M$OIS$WLW(long dividend, short divisor) { return (short)(dividend % divisor); }

struct aste_t *area_$get_aste(int16_t area_id, area_$seg_slot_t *slot,
                              int16_t seg_idx, int8_t in_trans_held,
                              int8_t create, status_$t *status_p)
{
    (void)area_id; (void)slot; (void)in_trans_held;
    get_seg[get_calls % LOGN] = (uint16_t)seg_idx;
    get_create[get_calls % LOGN] = create;
    get_calls++;
    *status_p = get_status;
    if (get_result != NULL) {
        return get_result;
    }
    return AST_ASTE_ENTRY(seg_idx + 1);
}

void AST_$FREE_PAGES(aste_t *aste, int16_t start_page, int16_t end_page,
                     int16_t vol_index)
{
    (void)aste;
    fp_start[frees_p % LOGN] = start_page;
    fp_end[frees_p % LOGN] = end_page;
    fp_vol = vol_index;
    frees_p++;
}

void AST_$FREE_ASTE(aste_t *aste) { fa[frees_a++ % LOGN] = aste; }
void DBUF_$INVALIDATE(int32_t block, uint16_t vol_idx)
{
    (void)vol_idx;
    inval_blk[invals++ % LOGN] = (uint32_t)block;
}

void BAT_$FREE(uint32_t *blocks, int16_t count, int16_t vol_idx,
               int16_t reserved, status_$t *status)
{
    int i;
    (void)reserved;
    for (i = 0; i < count && i < LOGN; i++) {
        bat_blk[i] = blocks[i];
    }
    bat_count = count;
    bat_vol = vol_idx;
    bats++;
    *status = status_$ok;
}

void area_$free_seg_table(area_$seg_table_t *entry, area_$seg_table_t *prev,
                          int16_t asid)
{
    tfrees++;
    tfree_tbl = entry; tfree_prev = prev; tfree_asid = asid;
}

#include "../free_segments.c"

static area_$entry_t *e;
static struct {
    area_$seg_table_t t0, t1;
    area_$seg_slot_t cells[0x100];
} arena;

static area_$seg_slot_t *cell(int n) { return (area_$seg_slot_t *)&e->seg_bitmap[n]; }

static void reset_state(void)
{
    memset(mock_area_table, 0, sizeof(mock_area_table));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    memset(&arena, 0, sizeof(arena));
    locks = unlocks = crashes = get_calls = frees_p = frees_a = 0;
    invals = bats = tfrees = 0;
    bat_count = 0;
    get_status = status_$ok;
    get_result = NULL;
    e = &mock_area_table[2];            /* area id 3 */
    e->volx = 4;
    e->owner_asid = 6;
    ARCH_HOST_VA_BASE = (uintptr_t)&arena - 0x1000u;
}

/* ---- tests ----------------------------------------------------------- */

static void test_clear_whole_segments(void)
{
    status_$t st = 0x99;
    aste_t *a1 = AST_ASTE_ENTRY(2), *a2 = AST_ASTE_ENTRY(3);

    /* segments 1 and 2 allocated, chain a2 -> a1, a2 is the head */
    cell(0)->bits = 0x06;
    cell(0)->state = AREA_SLOT_HAS_ASTE;
    cell(0)->aste_index = 3;
    a2->next = a1;
    a2->fm_block = (0x1234u << 4) | 2;

    area_$free_segments(3, 0x20, 0x5F, -1, &st);  /* segs 1..2, all pages */
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, get_calls);
    ASSERT_EQ(1, get_seg[0]);
    ASSERT_EQ(-1, get_create[0]);       /* local area: create */
    ASSERT_EQ(2, frees_p);
    ASSERT_EQ(0, fp_start[0]);
    ASSERT_EQ(0x1F, fp_end[1]);
    ASSERT_EQ(4, fp_vol);
    ASSERT_EQ(2, frees_a);
    ASSERT_EQ(0, cell(0)->bits);
    /* seg 1's ASTE (a1) was not the head: unlinked from a2 */
    ASSERT_EQ(0, (unsigned long)a2->next);
    /* seg 2's ASTE (a2) was then the only one: the block address returns */
    ASSERT_EQ(0, cell(0)->state & (AREA_SLOT_HAS_ASTE | AREA_SLOT_IN_TRANS));
    /* bits zero and an address: queued, invalidated, freed at the end,
     * and the cell's address cleared */
    ASSERT_EQ(1, invals);
    ASSERT_EQ(1, bats);
    ASSERT_EQ(1, bat_count);
    ASSERT_EQ(0x1234, bat_blk[0]);
    ASSERT_EQ(0x1234, inval_blk[0]);
    ASSERT_EQ(4, bat_vol);
    ASSERT_EQ(AREA_SLOT_LONG(cell(0)) & AREA_SLOT_DADDR_MASK, 0);
    ASSERT_EQ(locks, unlocks);
}

static void test_partial_segment_keeps_bit(void)
{
    status_$t st;
    aste_t *a = AST_ASTE_ENTRY(2);

    cell(0)->bits = 0x02;
    cell(0)->state = AREA_SLOT_HAS_ASTE;
    cell(0)->aste_index = 2;
    a->wire_count = 3;
    area_$free_segments(3, 0x25, 0x3A, -1, &st);   /* seg 1 pages 5..0x1A */
    ASSERT_EQ(5, fp_start[0]);
    ASSERT_EQ(0x1A, fp_end[0]);
    ASSERT_EQ(0x02, cell(0)->bits);
    ASSERT_EQ(2, a->wire_count);         /* not freed: unwired */
    ASSERT_EQ(0, frees_a);
}

static void test_not_clearing_unwires(void)
{
    status_$t st;
    aste_t *a = AST_ASTE_ENTRY(2);
    cell(0)->bits = 0x02;
    a->wire_count = 1;
    area_$free_segments(3, 0x20, 0x3F, 0, &st);
    ASSERT_EQ(0, a->wire_count);
    ASSERT_EQ(0x02, cell(0)->bits);
    ASSERT_EQ(0, invals);
}

static void test_inactive_segment_is_dropped(void)
{
    status_$t st;
    cell(1)->bits = 0x01;               /* segment 8 */
    AREA_SLOT_STORE(cell(1), AREA_SLOT_LONG(cell(1)) | 0x777);
    get_status = status_$ast_segment_not_deactivatable;
    area_$free_segments(3, 0x100, 0x11F, -1, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, frees_p);
    ASSERT_EQ(0, cell(1)->bits);
    ASSERT_EQ(1, bats);
    ASSERT_EQ(0x777, bat_blk[0]);
}

static void test_remote_forgets_rpmap_slot(void)
{
    status_$t st;
    e->remote_volx = 0x22;
    cell(0)->bits = 0;
    AREA_SLOT_STORE(cell(0), 0x55);
    AREA_$GLOBALS.rpmap_cache[1].word_04 = 0x22;
    AREA_$GLOBALS.rpmap_cache[1].word_06 = 0;
    AREA_$GLOBALS.rpmap_cache[1].seq = 9;
    area_$free_segments(3, 0, 0x1F, -1, &st);
    ASSERT_EQ(0, get_calls);
    ASSERT_EQ(0, AREA_$GLOBALS.rpmap_cache[1].seq);
    ASSERT_EQ(0, AREA_$GLOBALS.rpmap_cache[1].word_04);
    ASSERT_EQ(0, bats);
    ASSERT_EQ(0, AREA_SLOT_LONG(cell(0)) & AREA_SLOT_DADDR_MASK);
}

static void test_overflow_table_freed(void)
{
    status_$t st;
    /* cells 2.. live in table 0; free segments 16..23 (cell 2 = cell 0 of
     * table 0) to the end of the area */
    arena.t1.area_id = 3; arena.t1.table_index = 0;
    arena.t1.bitmap_ptr = ARCH_PTR_TO_VA(arena.cells);
    arena.t0.area_id = 9;
    arena.t0.next = ARCH_PTR_TO_VA(&arena.t1);
    AREA_$GLOBALS.seg_table_list[6] = &arena.t0;
    arena.cells[0].bits = 0;
    arena.cells[1].bits = 0;

    area_$free_segments(3, 0x200, 0x5FF, -1, &st);
    ASSERT_EQ(1, tfrees);
    ASSERT_EQ((unsigned long)&arena.t1, (unsigned long)tfree_tbl);
    ASSERT_EQ((unsigned long)&arena.t0, (unsigned long)tfree_prev);
    ASSERT_EQ(6, tfree_asid);
    ASSERT_EQ(0, crashes);
}

static void test_overflow_table_kept_when_partial(void)
{
    status_$t st;
    arena.t1.area_id = 3; arena.t1.table_index = 0;
    arena.t1.bitmap_ptr = ARCH_PTR_TO_VA(arena.cells);
    AREA_$GLOBALS.seg_table_list[6] = &arena.t1;
    /* starts at cell 3 of the table: first_cell <> 0 */
    area_$free_segments(3, 0x500, 0x5FF, -1, &st);
    ASSERT_EQ(0, tfrees);
}

int main(void)
{
    printf("area_$free_segments tests:\n");
    RUN_TEST(clear_whole_segments);
    RUN_TEST(partial_segment_keeps_bit);
    RUN_TEST(not_clearing_unwires);
    RUN_TEST(inactive_segment_is_dropped);
    RUN_TEST(remote_forgets_rpmap_slot);
    RUN_TEST(overflow_table_freed);
    RUN_TEST(overflow_table_kept_when_partial);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
