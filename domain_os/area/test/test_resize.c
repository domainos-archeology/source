/*
 * area/test/test_resize.c - unit tests for area_$resize (0x00E08816) and
 * its nested procedure area_$remote_sync (0x00E087BA)
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
#include "bat/bat.h"
#include "misc/crash_system.h"

area_$globals_t AREA_$GLOBALS;
status_$t Area_Internal_Error = 0x0032000A;

/* ---- mocks ----------------------------------------------------------- */

static int crashes, frees, cancels, reserves, grows, allocs, tfrees, locks;
static int16_t fs_area; static uint32_t fs_start, fs_end; static int8_t fs_clear;
static uint32_t cancel_n, reserve_n;
static int16_t bat_vol;
static uint16_t grow_volx; static uint32_t grow_size, grow_commit;
static void *grow_addr;
static int alloc_fail_at;               /* allocs index that returns NIL */
static int16_t alloc_tbl[16];
static area_$seg_table_t pool[4];
static area_$seg_table_t *tfree_tbl[8];
static status_$t cancel_status;

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; }
void CRASH_SYSTEM(const status_$t *s) { (void)s; crashes++; }

void area_$free_segments(int16_t area_id, uint32_t start_page,
                         uint32_t end_page, int8_t clear_bitmap,
                         status_$t *status_p)
{
    frees++;
    fs_area = area_id; fs_start = start_page; fs_end = end_page;
    fs_clear = clear_bitmap;
    *status_p = status_$ok;
}

void BAT_$CANCEL(int16_t vol_idx, uint32_t count, status_$t *status)
{ cancels++; bat_vol = vol_idx; cancel_n = count; *status = cancel_status; }
void BAT_$RESERVE(int16_t vol_idx, uint32_t count, status_$t *status)
{ reserves++; bat_vol = vol_idx; reserve_n = count; *status = status_$ok; }

void REM_FILE_$GROW_AREA(void *addr_info, uint16_t area_handle,
                         uint32_t current_size, uint32_t new_size,
                         status_$t *status)
{
    grows++;
    grow_addr = addr_info; grow_volx = area_handle;
    grow_size = current_size; grow_commit = new_size;
    *status = status_$ok;
}

area_$seg_table_t *area_$alloc_seg_table(int16_t asid, int16_t area_id,
                                          int16_t table_idx)
{
    (void)asid; (void)area_id;
    alloc_tbl[allocs & 15] = table_idx;
    if (allocs++ == alloc_fail_at) {
        return NULL;
    }
    return &pool[0];
}

void area_$free_seg_table(area_$seg_table_t *entry, area_$seg_table_t *prev,
                          int16_t asid)
{
    (void)prev; (void)asid;
    tfree_tbl[tfrees++ & 7] = entry;
}

#include "../resize.c"

static area_$entry_t e;

static void reset_state(void)
{
    memset(&e, 0, sizeof(e));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(pool, 0, sizeof(pool));
    crashes = frees = cancels = reserves = grows = allocs = tfrees = locks = 0;
    alloc_fail_at = -1;
    cancel_status = status_$ok;
    e.flags = AREA_FLAG_ACTIVE;
    e.volx = 2;
    e.owner_asid = 7;
    e.first_bste = -1;
    ARCH_HOST_VA_BASE = (uintptr_t)pool - 0x1000u;
}

/* ---- tests ----------------------------------------------------------- */

static void test_inactive(void)
{
    status_$t st;
    e.flags = 0;
    area_$resize(1, &e, 0x8000, 0, 0, &st);
    ASSERT_EQ(0x00320006, st);
}

static void test_commit_above_virt(void)
{
    status_$t st;
    area_$resize(1, &e, 0x7000, 0x8001, 0, &st);   /* 0x8000 vs 0x8400 */
    ASSERT_EQ(0x0032000B, st);
    ASSERT_EQ(0, e.virt_size);
}

static void test_local_grow_reserves(void)
{
    status_$t st = 0x55;
    area_$resize(1, &e, 0x10001, 0x801, 0, &st);   /* 0x18000, 0xC00 */
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x18000, e.virt_size);
    ASSERT_EQ(0xC00, e.commit_size);
    ASSERT_EQ(1, reserves);
    ASSERT_EQ(3, reserve_n);
    ASSERT_EQ(2, bat_vol);
    ASSERT_EQ(0, allocs);               /* still inside the inline cells */
}

static void test_local_shrink_cancels(void)
{
    status_$t st;
    e.virt_size = 0x40000;              /* 0x100 pages */
    e.commit_size = 0x20000;
    area_$resize(4, &e, 0x8000, 0, 0, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, frees);
    ASSERT_EQ(4, fs_area);
    ASSERT_EQ(0x20, fs_start);
    ASSERT_EQ(0xFF, fs_end);
    ASSERT_EQ(-1, fs_clear);
    ASSERT_EQ(1, cancels);
    ASSERT_EQ((0x20000 - 0x8000) >> 10, cancel_n);
    ASSERT_EQ(0x8000, e.commit_size);
    ASSERT_EQ(0x8000, e.virt_size);
    ASSERT_EQ(0, locks);                /* no BSTE: no PMAP-lock loop */
}

static void test_shrink_with_bste_takes_pmap_lock(void)
{
    status_$t st;
    e.virt_size = 0x40000;
    e.first_bste = 3;
    area_$resize(4, &e, 0x8000, 0, 0, &st);
    ASSERT_EQ(1, locks);
}

static void test_remote_shrink_syncs_partner(void)
{
    status_$t st;
    e.remote_volx = 0x33;
    e.virt_size = 0x40000;
    e.commit_size = 0x20000;
    area_$resize(4, &e, 0x8000, 0x400, 1, &st);
    ASSERT_EQ(1, grows);
    ASSERT_EQ((unsigned long)&AREA_$PARTNER, (unsigned long)grow_addr);
    ASSERT_EQ(0x33, grow_volx);
    ASSERT_EQ(0x8400, grow_size);       /* one extra 1K block */
    ASSERT_EQ(0x800, grow_commit);      /* 0x400 + 0x400 */
    ASSERT_EQ(0, cancels);
    ASSERT_EQ(0x8000, e.commit_size);
    ASSERT_EQ(0x8000, e.virt_size);
}

static void test_grow_allocates_tables(void)
{
    status_$t st;
    e.virt_size = 0x8000;               /* 0x20 pages, cell 0 */
    area_$resize(1, &e, 0x200000, 0, 0, &st);  /* 0x800 pages: cell 7 */
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, allocs);
    ASSERT_EQ(0, alloc_tbl[0]);
    ASSERT_EQ(0x200000, e.virt_size);
}

static void test_grow_table_failure_rolls_back(void)
{
    status_$t st;
    e.virt_size = 0x8000;
    e.remote_volx = 5;
    pool[1].area_id = 1; pool[1].table_index = 0;
    pool[1].next = ARCH_PTR_TO_VA(&pool[2]);
    pool[2].area_id = 9;
    AREA_$GLOBALS.seg_table_list[7] = &pool[1];
    alloc_fail_at = 0;
    area_$resize(1, &e, 0x200000, 0, 1, &st);
    ASSERT_EQ(0x00320005, st);
    ASSERT_EQ(1, tfrees);
    ASSERT_EQ((unsigned long)&pool[1], (unsigned long)tfree_tbl[0]);
    ASSERT_EQ(2, grows);                /* the sync, then the roll-back */
    ASSERT_EQ(0x8000, e.virt_size);
}

static void test_grow_into_dirty_inline_cell_crashes(void)
{
    status_$t st;
    e.virt_size = 0x8000;
    e.seg_bitmap[1] = 0x01000000;
    area_$resize(1, &e, 0x40000, 0, 0, &st);    /* cells 0..0 -> 0..0? */
    ASSERT_EQ(0, crashes);
    area_$resize(1, &e, 0x80000, 0, 0, &st);    /* 0x200 pages: cell 1 */
    ASSERT_EQ(1, crashes);
}

int main(void)
{
    printf("area_$resize tests:\n");
    RUN_TEST(inactive);
    RUN_TEST(commit_above_virt);
    RUN_TEST(local_grow_reserves);
    RUN_TEST(local_shrink_cancels);
    RUN_TEST(shrink_with_bste_takes_pmap_lock);
    RUN_TEST(remote_shrink_syncs_partner);
    RUN_TEST(grow_allocates_tables);
    RUN_TEST(grow_table_failure_rolls_back);
    RUN_TEST(grow_into_dirty_inline_cell_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
