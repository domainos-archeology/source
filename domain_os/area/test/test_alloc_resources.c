/*
 * area/test/test_alloc_resources.c - unit tests for area_$alloc_resources
 * (0x00E075CA)
 *
 * The area table is a host array (AREA_TABLE_BASE / AREA_ENTRY_SIZE are
 * redirected to it); MMU_$VTOP, WP_$CALLOC and MMU_$INSTALL are mocked.
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

#define TABLE_N 16
static area_$entry_t mock_area_table[TABLE_N];
#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)
#undef AREA_ENTRY_SIZE
#define AREA_ENTRY_SIZE ((int)sizeof(area_$entry_t))

area_$globals_t AREA_$GLOBALS;

static int vtop_calls, calloc_calls, install_calls;
static uint32_t vtop_va[32], install_va[32], install_flags;
static int mapped_upto;     /* entries below this index translate */

uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    uint32_t base = (uint32_t)(uintptr_t)mock_area_table;
    vtop_va[vtop_calls++ & 31] = va;
    *status = ((va - base) / sizeof(area_$entry_t) < (uint32_t)mapped_upto)
                  ? status_$ok : status_$mmu_miss;
    return 0;
}

void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    calloc_calls++;
    *ppn_out = 0x300 + calloc_calls;
    *status = status_$ok;
}

void (MMU_$INSTALL)(uint32_t ppn, uint32_t va, uint32_t flags)
{
    (void)ppn;
    install_va[install_calls++ & 31] = va;
    install_flags = flags;
}

static void reset_state(void)
{
    memset(mock_area_table, 0xEE, sizeof(mock_area_table));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    vtop_calls = calloc_calls = install_calls = 0;
    install_flags = 0;
    mapped_upto = TABLE_N;
}

#include "../alloc_resources.c"

static void test_extends_and_threads(void)
{
    area_$entry_t sentinel;
    boolean r;

    AREA_$FORMAT.max_entries = 10;
    AREA_$N_AREAS = 2;
    AREA_$N_FREE = 1;
    AREA_$FREE_LIST = &sentinel;

    r = area_$alloc_resources(3);
    ASSERT_EQ(0xFF, (uint8_t)r);
    ASSERT_EQ(5, AREA_$N_AREAS);
    ASSERT_EQ(4, AREA_$N_FREE);
    ASSERT_EQ((unsigned long)&mock_area_table[2], (unsigned long)AREA_$FREE_LIST);
    ASSERT_EQ((unsigned long)&mock_area_table[3], (unsigned long)mock_area_table[2].next);
    ASSERT_EQ((unsigned long)&mock_area_table[4], (unsigned long)mock_area_table[3].next);
    ASSERT_EQ((unsigned long)&sentinel, (unsigned long)mock_area_table[4].next);
    ASSERT_EQ(0, (unsigned long)mock_area_table[3].prev);
    ASSERT_EQ(3, mock_area_table[2].area_id);
    ASSERT_EQ(5, mock_area_table[4].area_id);
    ASSERT_EQ(0, mock_area_table[4].virt_size);   /* cleared */
    ASSERT_EQ(0, mock_area_table[4].flags);
    ASSERT_EQ(0xEEEE, (uint16_t)mock_area_table[5].flags);  /* untouched */
    /* one probe for the first entry, one per entry for its last byte */
    ASSERT_EQ(4, vtop_calls);
    ASSERT_EQ((uint32_t)(uintptr_t)&mock_area_table[2] + 0x2F, vtop_va[1]);
    ASSERT_EQ(0, calloc_calls);
}

static void test_clamps_to_max(void)
{
    AREA_$FORMAT.max_entries = 4;
    AREA_$N_AREAS = 1;
    ASSERT_EQ(0xFF, (uint8_t)area_$alloc_resources(0x60));
    ASSERT_EQ(4, AREA_$N_AREAS);
    ASSERT_EQ(3, AREA_$N_FREE);
    ASSERT_EQ(0, (unsigned long)mock_area_table[3].next);   /* old list NIL */
}

static void test_full_table_returns_false(void)
{
    AREA_$FORMAT.max_entries = 4;
    AREA_$N_AREAS = 4;
    ASSERT_EQ(0, (uint8_t)area_$alloc_resources(1));
    ASSERT_EQ(4, AREA_$N_AREAS);
    ASSERT_EQ(0, vtop_calls);
}

static void test_wires_unmapped_pages(void)
{
    AREA_$FORMAT.max_entries = 10;
    AREA_$N_AREAS = 0;
    mapped_upto = 1;               /* only entry 0 translates */
    area_$alloc_resources(2);
    /* entry 0 start and end translate; entry 1's end does not */
    ASSERT_EQ(1, calloc_calls);
    ASSERT_EQ(1, install_calls);
    ASSERT_EQ((uint32_t)(uintptr_t)&mock_area_table[1] + 0x2F, install_va[0]);
    ASSERT_EQ(0x16, install_flags);
}

static void test_single_entry(void)
{
    AREA_$FORMAT.max_entries = 10;
    AREA_$N_AREAS = 7;
    area_$alloc_resources(1);
    ASSERT_EQ(8, AREA_$N_AREAS);
    ASSERT_EQ(8, mock_area_table[7].area_id);
    ASSERT_EQ((unsigned long)&mock_area_table[7], (unsigned long)AREA_$FREE_LIST);
}

int main(void)
{
    printf("area_$alloc_resources tests:\n");
    RUN_TEST(extends_and_threads);
    RUN_TEST(clamps_to_max);
    RUN_TEST(full_table_returns_false);
    RUN_TEST(wires_unmapped_pages);
    RUN_TEST(single_entry);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
