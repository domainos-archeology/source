/*
 * mst/test/test_fork.c - unit tests for MST_$FORK (0x00E739F8)
 *
 * One host arena stands in for target memory from the AREA table
 * (0xD94C00) through the first MSTE pages (0xEF6400..).
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

#include "mst/mst_internal.h"

/* MST_PAGE_TABLE_BASE is ARCH_PTR_TO_VA(MSTE_PAGES), a sau2.ld symbol past
 * OS_PAGE_END since source-o7s2 (docs/rfc-cold-start.md section 8c); this
 * test stands in for the link with the map's value, `EF6400 MSTE_PAGES'
 * (os/test/test_vm_tables.c checks the macro itself). */
#undef MST_PAGE_TABLE_BASE
#define MST_PAGE_TABLE_BASE 0x00EF6400u
#include "anon/anon.h"
#include "area/area.h"

uint16_t MST[MST_TABLE_ENTRIES];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
uint16_t MST_$SEG_TN;
uint16_t PROC1_$AS_ID;
uid_t ANON_$UID;

#define ARENA_LO   0x00D94C00u
#define ARENA_SIZE (0x00EF7400u - ARENA_LO)
static uint8_t arena[ARENA_SIZE] __attribute__((aligned(0x400)));

/* ---- mocks ---- */
static int lock_calls, unlock_calls, alloc_calls, copy_calls;
static status_$t alloc_status, copy_status;
static uint16_t next_child_page;
static uint16_t alloc_seg[8];
static uint32_t copy_flags;
static int16_t copy_gen, copy_asid, copy_pid;
static uint16_t copy_id;

void ML_$LOCK(int16_t id)   { (void)id; lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlock_calls++; }
status_$t MST_$ALLOC_TABLE_PAGE(uint16_t asid, uint16_t seg, uint16_t *table_ptr)
{
    (void)asid;
    alloc_seg[alloc_calls++] = seg;
    if (alloc_status == 0) {
        *table_ptr = next_child_page++;
    }
    return alloc_status;
}
uint32_t AREA_$COPY(int16_t gen, uint16_t area_id, int16_t new_asid,
                    int16_t param_4, uint32_t stack_limit, status_$t *status_ret)
{
    copy_calls++;
    copy_gen = gen; copy_id = area_id; copy_asid = new_asid; copy_pid = param_4;
    copy_flags = stack_limit;
    *status_ret = copy_status;
    return 0x00090000u | (uint32_t)(0x20 + copy_calls);
}

#include "../fork.c"

static uint8_t *at(uint32_t va) { return arena + (va - ARENA_LO); }
static mst_entry_t *page_entry(uint16_t pg, int i)
{
    return (mst_entry_t *)(void *)at(MST_PAGE_TABLE_BASE + (pg - 1u) * 0x400u + i * 16u);
}
static area_$entry_t *area_entry(uint16_t id)
{
    return (area_$entry_t *)(void *)at(AREA_TABLE_BASE + (id - 1u) * AREA_ENTRY_SIZE);
}

static void reset_state(void)
{
    memset(MST, 0, sizeof(MST));
    memset(MST_ASID_BASE, 0, sizeof(MST_ASID_BASE));
    memset(arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - (uintptr_t)ARENA_LO;
    lock_calls = unlock_calls = alloc_calls = copy_calls = 0;
    alloc_status = copy_status = 0;
    next_child_page = 3;
    MST_$SEG_TN = 0x80;                 /* two table pages per asid */
    PROC1_$AS_ID = 2;
    MST_ASID_BASE[2] = 0x10;
    MST_ASID_BASE[5] = 0x20;
    MST[0x11] = 1;                      /* parent: only its 2nd page */
    ANON_$UID.high = 0x11; ANON_$UID.low = 0;
}

static void test_copies_entries_and_areas(void)
{
    status_$t st = -1;
    mst_entry_t *e;

    /* entry 0: a plain object */
    e = page_entry(1, 0); e->uid.high = 0x0100; e->uid.low = 0x77; e->flags = 0x1234;
    /* entries 1 and 2: the same private anon area (gen 4, id 3) */
    area_entry(3)->generation = 4; area_entry(3)->owner_asid = 2;
    e = page_entry(1, 1); e->uid = ANON_$UID; e->uid.low = 0x00040003; e->flags = 0x0FFF;
    e = page_entry(1, 2); e->uid = ANON_$UID; e->uid.low = 0x00040003; e->flags = 0x0201;
    /* entry 3: an anon area owned by someone else - copied verbatim */
    area_entry(6)->generation = 1; area_entry(6)->owner_asid = 9;
    e = page_entry(1, 3); e->uid = ANON_$UID; e->uid.low = 0x00010006; e->flags = 0x0033;
    /* entry 63 */
    e = page_entry(1, 63); e->uid.high = 0x5; e->uid.low = 0x6;

    MST_$FORK(5, 0x44, 0xABCD, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(1, alloc_calls);
    ASSERT_EQ(0x40, alloc_seg[0]);
    ASSERT_EQ(3, MST[0x21]);
    ASSERT_EQ(0, MST[0x20]);
    ASSERT_EQ(0x77, page_entry(3, 0)->uid.low);
    ASSERT_EQ(0x1234, page_entry(3, 0)->flags);
    ASSERT_EQ(1, copy_calls);           /* one copy for the run */
    ASSERT_EQ(4, copy_gen);
    ASSERT_EQ(3, copy_id);
    ASSERT_EQ(5, copy_asid);
    ASSERT_EQ(0x44, copy_pid);
    ASSERT_EQ(0xABCD, copy_flags);
    ASSERT_EQ(0x00090021, page_entry(3, 1)->uid.low);
    ASSERT_EQ(0x0E00, page_entry(3, 1)->flags);
    ASSERT_EQ(0x00090021, page_entry(3, 2)->uid.low);
    ASSERT_EQ(0x0200, page_entry(3, 2)->flags);
    ASSERT_EQ(0x00010006, page_entry(3, 3)->uid.low);
    ASSERT_EQ(0x0033, page_entry(3, 3)->flags);
    ASSERT_EQ(0x6, page_entry(3, 63)->uid.low);
}

static void test_generation_mismatch_not_copied(void)
{
    status_$t st = -1;
    mst_entry_t *e;
    area_entry(3)->generation = 5; area_entry(3)->owner_asid = 2;
    e = page_entry(1, 0); e->uid = ANON_$UID; e->uid.low = 0x00040003; e->flags = 0x0FFF;
    MST_$FORK(5, 0, 0, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, copy_calls);
    ASSERT_EQ(0x0FFF, page_entry(3, 0)->flags);
}

static void test_alloc_failure(void)
{
    status_$t st = 0;
    alloc_status = 0x00040003;
    MST_$FORK(5, 0, 0, &st);
    ASSERT_EQ(0x00040003, st);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, copy_calls);
}

static void test_copy_failure(void)
{
    status_$t st = 0;
    mst_entry_t *e;
    area_entry(3)->generation = 4; area_entry(3)->owner_asid = 2;
    e = page_entry(1, 0); e->uid = ANON_$UID; e->uid.low = 0x00040003;
    copy_status = 0x00070005;
    MST_$FORK(5, 0, 0, &st);
    ASSERT_EQ(0x00070005, st);
    ASSERT_EQ(1, unlock_calls);
}

static void test_no_segments(void)
{
    status_$t st = -1;
    MST_$SEG_TN = 0x3F;                 /* 0 pages: both loops skipped */
    MST_$FORK(5, 0, 0, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, alloc_calls);
    ASSERT_EQ(1, unlock_calls);
}

int main(void)
{
    printf("MST_$FORK tests\n");
    RUN_TEST(copies_entries_and_areas);
    RUN_TEST(generation_mismatch_not_copied);
    RUN_TEST(alloc_failure);
    RUN_TEST(copy_failure);
    RUN_TEST(no_segments);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
