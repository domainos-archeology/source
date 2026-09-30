/*
 * mst/test/test_grow_area.c - unit tests for MST_$GROW_AREA (0x00E4360C)
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
#include "anon/anon.h"

/* The area table is a fixed image address; point it at a local array. */
static uint8_t mock_area_table[0x30 * 4 + 0x40] __attribute__((aligned(16)));
#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)

/* ---- data ------------------------------------------------------------ */

uint16_t PROC1_$AS_ID = 5;
uid_t ANON_$UID = { 0xA0A00000u, 0 };
static mste_t the_mste;

/* ---- mocks ----------------------------------------------------------- */

static status_$t pte_status, alloc_status, grow_status;
static int n_alloc, n_grow, n_unmap;
static uint32_t alloc_addr, alloc_start, alloc_len;
static uint16_t alloc_area_id, alloc_touch;
static uint8_t alloc_access;
static uint32_t grow_size, grow_commit;
static int16_t grow_gen;
static uint16_t grow_id;
static uint32_t unmap_start, unmap_size;
static status_$t *unmap_status_ptr;

void mst_$va_to_pte(uint16_t asid, uint32_t va, uint16_t *prot, void **entry,
                    status_$t *status)
{
    (void)asid; (void)va; (void)prot;
    *entry = &the_mste;
    *status = pte_status;
}
void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                      uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, boolean direction, void *map_info,
                      status_$t *status)
{
    (void)uid; (void)area_size; (void)asid; (void)direction; (void)map_info;
    n_alloc++; alloc_addr = addr_hint; alloc_start = start_va; alloc_len = length;
    alloc_area_id = area_id; alloc_touch = touch_count; alloc_access = access_rights;
    *status = alloc_status;
    return NULL;
}
void AREA_$GROW(int16_t gen, uint16_t id, uint32_t size, uint32_t commit,
                status_$t *status)
{
    n_grow++; grow_gen = gen; grow_id = id; grow_size = size; grow_commit = commit;
    *status = grow_status;
}
void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status)
{
    (void)mode; (void)uid; (void)asid;
    n_unmap++; unmap_start = start; unmap_size = size; unmap_status_ptr = status;
    *status = 0x77;
}

#include "../grow_area.c"

/* ---- helpers --------------------------------------------------------- */

static area_$entry_t *area1(void) { return (area_$entry_t *)(void *)mock_area_table; }

static void reset_state(void)
{
    memset(mock_area_table, 0, sizeof(mock_area_table));
    pte_status = alloc_status = grow_status = 0;
    n_alloc = n_grow = n_unmap = 0;
    the_mste.uid.high = ANON_$UID.high;
    the_mste.uid.low = 0x00090001;              /* generation 9, area 1 */
    the_mste.segment = 2;
    area1()->generation = 9;
    area1()->virt_size = 0x10000;               /* 2 segments */
    area1()->flags = 0;
}

/* ---- tests ----------------------------------------------------------- */

static void test_not_an_area(void)
{
    uint32_t va = 0x100000, size = 0x20000, commit = 0;
    status_$t st;
    the_mste.uid.high = 0x1234;
    MST_$GROW_AREA(&va, &size, &commit, &st);
    ASSERT_EQ(0x00040012, st);
    the_mste.uid.high = ANON_$UID.high;
    the_mste.uid.low = 0x00090541;
    MST_$GROW_AREA(&va, &size, &commit, &st);
    ASSERT_EQ(0x00040012, st);
}

static void test_stale_generation(void)
{
    uint32_t va = 0x100000, size = 0x20000, commit = 0;
    status_$t st;
    area1()->generation = 8;
    MST_$GROW_AREA(&va, &size, &commit, &st);
    ASSERT_EQ(0x00040001, st);
    ASSERT_EQ(0, n_grow);
}

static void test_grow_maps_new_segments(void)
{
    uint32_t va = 0x100000, size = 0x20000, commit = 0x5000;   /* 4 segments */
    status_$t st;
    MST_$GROW_AREA(&va, &size, &commit, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, n_alloc);
    /* (0x100000>>15) - 2 + 2 = 0x20 */
    ASSERT_EQ(0x20u << 15, alloc_addr);
    ASSERT_EQ(2u << 15, alloc_start);
    ASSERT_EQ(2u << 15, alloc_len);
    ASSERT_EQ(7, alloc_area_id);
    ASSERT_EQ(1, alloc_touch);
    ASSERT_EQ(0xFF, alloc_access);
    ASSERT_EQ(1, n_grow);
    ASSERT_EQ(9, grow_gen);
    ASSERT_EQ(1, grow_id);
    ASSERT_EQ(0x20000, grow_size);
    ASSERT_EQ(0x5000, grow_commit);
    ASSERT_EQ(0, n_unmap);
}

static void test_grow_reversed_and_failure_unmaps(void)
{
    uint32_t va = 0x100000, size = 0x20000, commit = 0;
    status_$t st;
    area1()->flags = AREA_FLAG_REVERSED;
    grow_status = 0x0008000D;
    MST_$GROW_AREA(&va, &size, &commit, &st);
    /* 0x20 + (0xFFFF - 2) - 4 + 1 = 0x1001A */
    ASSERT_EQ(0x1001Au << 15, alloc_addr);
    ASSERT_EQ((0x10000u - 4) << 15, alloc_start);
    ASSERT_EQ(1, n_unmap);
    ASSERT_EQ(0x1001Au << 15, unmap_start);
    ASSERT_EQ(2u << 15, unmap_size);
    ASSERT_EQ(0x0008000D, st);                  /* the local took the unmap's */
}

static void test_grow_within_segment(void)
{
    uint32_t va = 0x100000, size = 0x0F000, commit = 0;
    status_$t st;
    area1()->virt_size = 0x9000;                /* 2 segments either way */
    MST_$GROW_AREA(&va, &size, &commit, &st);
    ASSERT_EQ(0, n_alloc);
    ASSERT_EQ(1, n_grow);
}

static void test_shrink_unmaps_tail(void)
{
    uint32_t va = 0x100000, size = 0x8000, commit = 0;   /* 1 segment */
    status_$t st;
    MST_$GROW_AREA(&va, &size, &commit, &st);
    ASSERT_EQ(1, n_grow);
    ASSERT_EQ(1, n_unmap);
    /* (0x100000>>15) - 2 + 1 = 0x1F */
    ASSERT_EQ(0x1Fu << 15, unmap_start);
    ASSERT_EQ(1u << 15, unmap_size);
    ASSERT_EQ((unsigned long)&st, (unsigned long)unmap_status_ptr);
}

static void test_alloc_failure(void)
{
    uint32_t va = 0x100000, size = 0x20000, commit = 0;
    status_$t st;
    alloc_status = 0x00040003;
    MST_$GROW_AREA(&va, &size, &commit, &st);
    ASSERT_EQ(0x00040003, st);
    ASSERT_EQ(0, n_grow);
}

int main(void)
{
    printf("MST_$GROW_AREA tests:\n");
    RUN_TEST(not_an_area);
    RUN_TEST(stale_generation);
    RUN_TEST(grow_maps_new_segments);
    RUN_TEST(grow_reversed_and_failure_unmaps);
    RUN_TEST(grow_within_segment);
    RUN_TEST(shrink_unmaps_tail);
    RUN_TEST(alloc_failure);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
