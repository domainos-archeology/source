/*
 * area/test/test_free_seg_table.c - unit tests for area_$free_seg_table
 * (0x00E09E48)
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
#include "mmu/mmu.h"
#include "mmap/mmap.h"

area_$globals_t AREA_$GLOBALS;

static char log_buf[64];
static void note(const char *s) { strcat(log_buf, s); }
static status_$t vtop_status;
static uint32_t vtop_va, removed, freed;

void ML_$LOCK(int16_t id) { if (id != 0x12) note("!"); note("L"); }
void ML_$UNLOCK(int16_t id) { if (id != 0x12) note("!"); note("U"); }
uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    note("V"); vtop_va = va; *status = vtop_status; return 0x321;
}
void MMU_$REMOVE(uint32_t ppn) { note("R"); removed = ppn; }
void MMAP_$FREE(uint32_t vpn) { note("F"); freed = vpn; }

#include "../free_seg_table.c"

static area_$seg_table_t *pool = AREA_$GLOBALS.seg_table_pool;

static void reset_state(void)
{
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    ARCH_HOST_VA_BASE = (uintptr_t)&AREA_$GLOBALS - 0x00E1E118;
    log_buf[0] = 0;
    vtop_status = 0; vtop_va = removed = freed = 0;
    /* list[3]: pool[4] -> pool[9] */
    pool[4].allocated = pool[9].allocated = (int8_t)0xFF;
    pool[4].bitmap_ptr = AREA_PIT_PAGES_VA + 4 * 0x400;
    pool[9].bitmap_ptr = AREA_PIT_PAGES_VA + 9 * 0x400;
    pool[4].next = ARCH_PTR_TO_VA(&pool[9]);
    AREA_$GLOBALS.seg_table_list[3] = &pool[4];
    AREA_$GLOBALS.format.seg_table_count = 2;
    AREA_$GLOBALS.format.seg_table_next = 10;
}

static void test_free_head(void)
{
    area_$free_seg_table(&pool[4], NULL, 3);
    ASSERT_EQ(0, strcmp(log_buf, "LVRFU"));
    ASSERT_EQ(AREA_PIT_PAGES_VA + 0x1000, vtop_va);
    ASSERT_EQ(0x321, removed);
    ASSERT_EQ(0x321, freed);
    ASSERT_EQ((unsigned long)&pool[9], (unsigned long)AREA_$GLOBALS.seg_table_list[3]);
    ASSERT_EQ(0, pool[4].next);
    ASSERT_EQ(0, pool[4].allocated);
    ASSERT_EQ(1, AREA_$GLOBALS.format.seg_table_count);
    ASSERT_EQ(4, AREA_$GLOBALS.format.seg_table_next);
}

static void test_free_middle_unmapped_keeps_lower_cursor(void)
{
    vtop_status = 0x00050001;
    AREA_$GLOBALS.format.seg_table_next = 2;
    area_$free_seg_table(&pool[9], &pool[4], 3);
    ASSERT_EQ(0, strcmp(log_buf, "LVU"));
    ASSERT_EQ(0, pool[4].next);
    ASSERT_EQ((unsigned long)&pool[4], (unsigned long)AREA_$GLOBALS.seg_table_list[3]);
    ASSERT_EQ(2, AREA_$GLOBALS.format.seg_table_next);
}

int main(void)
{
    printf("area_$free_seg_table tests\n");
    RUN_TEST(free_head);
    RUN_TEST(free_middle_unmapped_keeps_lower_cursor);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
