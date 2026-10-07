/*
 * area/test/test_alloc_seg_table.c - unit tests for area_$alloc_seg_table
 * (0x00E09D2E).  The PIT_PAGES window lives in a host arena reached through
 * ARCH_HOST_VA_BASE.
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

/* AREA_RPMAP_CACHE_VA and AREA_PIT_PAGES_VA are ARCH_PTR_TO_VA of the
 * sau2.ld symbols AREA_$RPMAP_CACHE and PIT_PAGES, past OS_PAGE_END since
 * source-o7s2 (docs/rfc-cold-start.md section 8c); this test stands in for
 * the link with the map's values (os/test/test_vm_tables.c checks the
 * macros themselves). */
#undef AREA_RPMAP_CACHE_VA
#define AREA_RPMAP_CACHE_VA 0x00EE4C00u
#undef AREA_PIT_PAGES_VA
#define AREA_PIT_PAGES_VA 0x00EE6400u
#include "wp/wp.h"
#include "mmu/mmu.h"

area_$globals_t AREA_$GLOBALS;

static uint8_t arena[64 * 0x400];
static char log_buf[64];
static void note(const char *s) { strcat(log_buf, s); }
static uint32_t inst_va, inst_flags, inst_ppn;

void ML_$LOCK(int16_t id) { if (id != 0x12) note("!"); note("L"); }
void ML_$UNLOCK(int16_t id) { if (id != 0x12) note("!"); note("U"); }
void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    note("W");
    *ppn_out = 0x123;
    *status = 0x00050001;           /* ignored by the caller */
}
void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    note("I");
    inst_ppn = ppn; inst_va = va; inst_flags = flags;
}

#include "../alloc_seg_table.c"

static void reset_state(void)
{
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(arena, 0xA5, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - AREA_PIT_PAGES_VA;
    log_buf[0] = 0;
    inst_va = inst_flags = inst_ppn = 0;
}

static void test_first_allocation(void)
{
    area_$seg_table_t *r = area_$alloc_seg_table(5, 0x22, 0x1A7);
    ASSERT_EQ((unsigned long)&AREA_$GLOBALS.seg_table_pool[0], (unsigned long)r);
    ASSERT_EQ(0, strcmp(log_buf, "LWIU"));
    ASSERT_EQ(0x123, inst_ppn);
    ASSERT_EQ(AREA_PIT_PAGES_VA, inst_va);
    ASSERT_EQ(0x16, inst_flags);
    ASSERT_EQ(0xFF, (uint8_t)r->allocated);
    ASSERT_EQ(0x22, r->area_id);
    ASSERT_EQ(0xA7, r->table_index);
    ASSERT_EQ(AREA_PIT_PAGES_VA, r->bitmap_ptr);
    ASSERT_EQ(0, r->next);
    ASSERT_EQ((unsigned long)r, (unsigned long)AREA_$GLOBALS.seg_table_list[5]);
    ASSERT_EQ(1, AREA_$GLOBALS.format.seg_table_count);
    ASSERT_EQ(1, AREA_$GLOBALS.format.seg_table_next);
    ASSERT_EQ(0, arena[0]);
    ASSERT_EQ(0, arena[0x3FF]);
    ASSERT_EQ(0xA5, arena[0x400]);
}

static void test_second_links_and_skips_used(void)
{
    area_$seg_table_t *a = area_$alloc_seg_table(5, 1, 0);
    area_$seg_table_t *b;
    AREA_$GLOBALS.seg_table_pool[2].allocated = (int8_t)0xFF;
    b = area_$alloc_seg_table(5, 1, 1);
    ASSERT_EQ((unsigned long)&AREA_$GLOBALS.seg_table_pool[1], (unsigned long)b);
    ASSERT_EQ(ARCH_PTR_TO_VA(a), b->next);
    ASSERT_EQ(AREA_PIT_PAGES_VA + 0x400, b->bitmap_ptr);
    ASSERT_EQ(3, AREA_$GLOBALS.format.seg_table_next);
}

static void test_scan_finds_nothing_keeps_cursor(void)
{
    int i;
    for (i = 0; i < 64; i++) AREA_$GLOBALS.seg_table_pool[i].allocated = (int8_t)0xFF;
    AREA_$GLOBALS.seg_table_pool[10].allocated = 0;
    AREA_$GLOBALS.format.seg_table_next = 10;
    AREA_$GLOBALS.format.seg_table_count = 3;
    area_$alloc_seg_table(0, 1, 0);
    ASSERT_EQ(10, AREA_$GLOBALS.format.seg_table_next);   /* no wrap */
}

static void test_last_record_sets_cursor_64(void)
{
    AREA_$GLOBALS.format.seg_table_count = 63;
    AREA_$GLOBALS.format.seg_table_next = 7;
    area_$alloc_seg_table(0, 1, 0);
    ASSERT_EQ(64, AREA_$GLOBALS.format.seg_table_count);
    ASSERT_EQ(64, AREA_$GLOBALS.format.seg_table_next);
}

static void test_full_pool_returns_nil(void)
{
    AREA_$GLOBALS.format.seg_table_count = 64;
    ASSERT_EQ(0, (unsigned long)area_$alloc_seg_table(0, 1, 0));
    ASSERT_EQ(0, strcmp(log_buf, "LU"));
}

int main(void)
{
    printf("area_$alloc_seg_table tests\n");
    RUN_TEST(first_allocation);
    RUN_TEST(second_links_and_skips_used);
    RUN_TEST(scan_finds_nothing_keeps_cursor);
    RUN_TEST(last_record_sets_cursor_64);
    RUN_TEST(full_pool_returns_nil);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
