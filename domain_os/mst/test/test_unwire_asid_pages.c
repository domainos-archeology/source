/*
 * mst/test/test_unwire_asid_pages.c - unit tests for mst_$unwire_asid_pages
 * (0x00E74AB6) and mst_$unwire_page (0x00E74A74)
 *
 * MST, the availability bitmap, the hint and the wired count are host
 * objects; MMU_$VTOP / MMU_$REMOVE / MMAP_$FREE / CRASH_SYSTEM are mocked.
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
#include "misc/crash_system.h"

/* ---- data ------------------------------------------------------------ */

uint16_t MST[MST_TABLE_ENTRIES];
uint32_t MST_$PAGE_AVAIL_BITMAP[MST_$PAGE_AVAIL_BITMAP_LONGS];
uint16_t MST_$PAGE_ALLOC_HINT;
uint16_t MST_$MST_PAGES_WIRED;

/* ---- mocks ----------------------------------------------------------- */

static int vtop_calls, remove_calls, free_calls, crash_calls;
static uint32_t vtop_va[8], remove_ppn[8], free_ppn[8];
static status_$t vtop_status, crash_status;

uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    vtop_va[vtop_calls & 7] = va;
    vtop_calls++;
    *status = vtop_status;
    return 0x500 + ((va - MST_PAGE_TABLE_BASE) >> 10);
}

void MMU_$REMOVE(uint32_t ppn) { remove_ppn[remove_calls++ & 7] = ppn; }
void MMAP_$FREE(uint32_t vpn) { free_ppn[free_calls++ & 7] = vpn; }

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_status = *status_p;
    crash_calls++;
}

static void reset_state(void)
{
    memset(MST, 0, sizeof(MST));
    memset(MST_$PAGE_AVAIL_BITMAP, 0, sizeof(MST_$PAGE_AVAIL_BITMAP));
    MST_$PAGE_ALLOC_HINT = 11;
    MST_$MST_PAGES_WIRED = 20;
    vtop_calls = remove_calls = free_calls = crash_calls = 0;
    vtop_status = status_$ok;
    crash_status = 0;
}

#include "../unwire_page.c"
#include "../unwire_asid_pages.c"

/* ---- tests ----------------------------------------------------------- */

static void test_frees_nonzero_slots(void)
{
    MST[0x40] = 0x45;           /* word 2, bit 5 */
    MST[0x42] = 0x21;           /* word 1, bit 1 */
    mst_$unwire_asid_pages(0x40, 0x43);

    ASSERT_EQ(2, vtop_calls);
    ASSERT_EQ(MST_PAGE_TABLE_BASE + 0x44 * 0x400, vtop_va[0]);
    ASSERT_EQ(MST_PAGE_TABLE_BASE + 0x20 * 0x400, vtop_va[1]);
    ASSERT_EQ(2, remove_calls);
    ASSERT_EQ(0x544, remove_ppn[0]);
    ASSERT_EQ(0x544, free_ppn[0]);
    ASSERT_EQ(0, MST[0x40]);
    ASSERT_EQ(0, MST[0x42]);
    ASSERT_EQ(1u << 5, MST_$PAGE_AVAIL_BITMAP[2]);
    ASSERT_EQ(1u << 1, MST_$PAGE_AVAIL_BITMAP[1]);
    ASSERT_EQ(1, MST_$PAGE_ALLOC_HINT);
    ASSERT_EQ(18, MST_$MST_PAGES_WIRED);
}

static void test_hint_only_lowers(void)
{
    MST_$PAGE_ALLOC_HINT = 1;
    MST[3] = 0x45;
    mst_$unwire_asid_pages(3, 3);
    ASSERT_EQ(1, MST_$PAGE_ALLOC_HINT);
    ASSERT_EQ(19, MST_$MST_PAGES_WIRED);
}

static void test_reversed_range_does_nothing(void)
{
    MST[5] = 7;
    mst_$unwire_asid_pages(6, 5);
    ASSERT_EQ(0, vtop_calls);
    ASSERT_EQ(7, MST[5]);
    ASSERT_EQ(20, MST_$MST_PAGES_WIRED);
}

static void test_translation_failure_crashes(void)
{
    vtop_status = 0x00050001;
    MST[0] = 1;
    mst_$unwire_asid_pages(0, 0);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00050001, crash_status);
    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(0, free_calls);
    /* the caller still frees the slot */
    ASSERT_EQ(0, MST[0]);
    ASSERT_EQ(1u << 1, MST_$PAGE_AVAIL_BITMAP[0]);
}

int main(void)
{
    printf("mst_$unwire_asid_pages tests:\n");
    RUN_TEST(frees_nonzero_slots);
    RUN_TEST(hint_only_lowers);
    RUN_TEST(reversed_range_does_nothing);
    RUN_TEST(translation_failure_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
