/*
 * mmap/test/test_alloc_pure.c - Unit tests for MMAP_$ALLOC_PURE (0x00E0D78E)
 *
 * The real mmap/alloc_pure.c and mmap/alloc_pages_from_wsl.c are #included
 * below and driven through a host copy of the MMAP module data area.
 *
 * The point under test: the pool cursor is A5+0x24, so `tst.l (0x30,A2)'
 * (0x00E0D7C4) reads MMAP_$WSL[1].page_count and the two-pass `dbf D5'
 * covers pools 1 (pure) and 2 (impure).  The free pool (0) is never
 * touched by this routine.
 */

#include <stdio.h>
#include <string.h>

#include "mmap/mmap_internal.h"
#include "proc1/proc1.h"
#include "arch/arch.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* Module data */
#define TEST_PAGES 64
static uint32_t pft_store[0x1000];   /* the PFT is indexed by ppn, up to 0xFFF */
MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
/* The table holds ppn 0x200..0xFFF (mmap/mmap.h), so the test's page n is
 * ppn VP(n). */
#define VP(n) (MMAP_MMAPE_FIRST_PPN + (n))
uint32_t *mmu_pft_base    = pft_store;
uint16_t PROC1_$CURRENT;

/* Mocks */
static int      trim_calls;
static uint16_t trim_index;
static uint32_t trim_count;

void mmap_$trim_wsl(uint16_t wsl_index, uint32_t pages_to_trim)
{
    trim_calls++;
    trim_index = wsl_index;
    trim_count = pages_to_trim;
}

#include "../alloc_pages_from_wsl.c"
#include "../alloc_pure.c"

/* Build a circular list: head -> prev_vpn -> ... ; tail = head.next_vpn */
static void build_list(uint16_t pool, const uint16_t *v, int n)
{
    ws_hdr_t *wsl = &MMAP_$WSL[pool];
    int i;

    wsl->head_vpn = v[0];
    wsl->page_count = (uint32_t)n;
    for (i = 0; i < n; i++) {
        mmape_t *p = MMAPE_FOR_VPN(v[i]);
        p->wsl_index = (uint8_t)pool;
        p->flags1 = MMAPE_FLAG1_IN_WSL;
        p->prev_vpn = v[(i + 1) % n];
        p->next_vpn = v[(i - 1 + n) % n];
    }
}

static void reset_module(void)
{
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    trim_calls = 0;
    PROC1_$CURRENT = 3;
    MMAP_PID_TO_WSL[3] = 7;
    MMAP_$PAGEABLE_PAGES = 1000;
}

TEST(takes_from_pure_then_impure_not_free)
{
    static const uint16_t free_v[] = { VP(1), VP(2), VP(3), VP(4), VP(5) };
    static const uint16_t pure_v[] = { VP(10), VP(11) };
    static const uint16_t impure_v[] = { VP(20), VP(21), VP(22) };
    uint32_t out[8] = { 0 };
    uint16_t got;

    reset_module();
    build_list(MMAP_WSL_POOL_FREE, free_v, 5);
    build_list(MMAP_WSL_POOL_PURE, pure_v, 2);
    build_list(MMAP_WSL_POOL_IMPURE, impure_v, 3);

    got = MMAP_$ALLOC_PURE(out, 4);

    ASSERT_EQ(4, got);
    ASSERT_EQ(VP(10), out[0]);
    ASSERT_EQ(VP(11), out[1]);
    ASSERT_EQ(VP(20), out[2]);
    ASSERT_EQ(VP(21), out[3]);
    ASSERT_EQ(5, MMAP_$WSL[MMAP_WSL_POOL_FREE].page_count);   /* untouched */
    ASSERT_EQ(0, MMAP_$WSL[MMAP_WSL_POOL_PURE].page_count);
    ASSERT_EQ(1, MMAP_$WSL[MMAP_WSL_POOL_IMPURE].page_count);
    ASSERT_EQ(VP(22), MMAP_$WSL[MMAP_WSL_POOL_IMPURE].head_vpn);
    ASSERT_EQ(1, MMAP_$ALLOC_CNT);
    ASSERT_EQ(4, MMAP_$ALLOC_PAGES);
    ASSERT_EQ(0, MMAP_$STEAL_CNT);
    ASSERT_EQ(0, trim_calls);
    ASSERT_EQ(1000 - 4, MMAP_$PAGEABLE_PAGES);
}

TEST(empty_pools_small_ws_no_trim)
{
    uint32_t out[8] = { 0 };
    uint16_t got;

    reset_module();
    MMAP_$WSL[7].page_count = 0x17F;   /* below the 0x180 floor */

    got = MMAP_$ALLOC_PURE(out, 3);

    ASSERT_EQ(0, got);
    ASSERT_EQ(1, MMAP_$STEAL_CNT);
    ASSERT_EQ(0, trim_calls);
    ASSERT_EQ(0, MMAP_$ALLOC_PAGES);
}

TEST(empty_pools_big_ws_trims_once)
{
    uint32_t out[8] = { 0 };
    uint16_t got;

    reset_module();
    MMAP_$WSL[7].page_count = 0x200;

    got = MMAP_$ALLOC_PURE(out, 3);

    ASSERT_EQ(0, got);
    ASSERT_EQ(1, trim_calls);
    ASSERT_EQ(7, trim_index);
    ASSERT_EQ(3, trim_count);
    ASSERT_EQ(2, MMAP_$STEAL_CNT);   /* second pass gives up: trimmed */
}

TEST(too_many_dirty_pages_blocks_trim)
{
    uint32_t out[8] = { 0 };

    reset_module();
    MMAP_$WSL[7].page_count = 0x200;
    MMAP_$WSL[MMAP_WSL_POOL_DIRTY_LOCAL].page_count = 5;
    MMAP_$WSL[MMAP_WSL_POOL_DIRTY_RMT].page_count = 4;   /* sum 9 > 8 */

    (void)MMAP_$ALLOC_PURE(out, 3);

    ASSERT_EQ(1, MMAP_$STEAL_CNT);
    ASSERT_EQ(0, trim_calls);
}

int main(void)
{
    printf("MMAP_$ALLOC_PURE tests\n");
    RUN_TEST(takes_from_pure_then_impure_not_free);
    RUN_TEST(empty_pools_small_ws_no_trim);
    RUN_TEST(empty_pools_big_ws_trims_once);
    RUN_TEST(too_many_dirty_pages_blocks_trim);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
