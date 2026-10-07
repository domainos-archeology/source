/*
 * mmap/test/test_trim_wsl.c - Unit tests for mmap_$trim_wsl (0x00E0C760),
 * from mmap/internal.c
 *
 * Covers the re-emission fixes: the seg-map bit cleared before MMU_$REMOVE
 * is bit 5 of the entry's flags byte (`bclr.b #5` at 0x00E0C85E, bit 13 of
 * the word), the purge exit clears ws_hdr_t.owner (+2, `clr.w (0x2e,A0)`)
 * rather than scan_pos, wired pages are unlinked but never re-pooled, and
 * the 32-page REFERENCED skip window.
 */

#include <stdio.h>
#include <string.h>

#include "mmap/mmap_internal.h"
#include "mmu/mmu.h"
#include "time/time.h"
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

#define TEST_PAGES 128
#define TEST_SEGMENTS 4
static uint32_t pft_store[0x1000];   /* the PFT is indexed by ppn, up to 0xFFF */
MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
/* The table holds ppn 0x200..0xFFF (mmap/mmap.h), so the test's page n is
 * ppn VP(n). */
#define VP(n) (MMAP_MMAPE_FIRST_PPN + (n))
#define SAU2_PFT_BASE pft_store   /* the SAU2 PFT (arch/m68k/sau2/hw.h) */
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
static aote_t aote_store[TEST_SEGMENTS];
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

static int      remove_calls;
static uint32_t remove_ppn[8];

void MMU_$REMOVE(uint32_t ppn)
{
    if (remove_calls < 8) remove_ppn[remove_calls] = ppn;
    remove_calls++;
}

#include "../internal.c"

static uint16_t list_v[TEST_PAGES];

/* circular list on WSL 7, all pages in segment 1 at seg_offset i */
static void build_list(int n)
{
    ws_hdr_t *wsl = &MMAP_$WSL[7];
    int i;

    for (i = 0; i < n; i++) list_v[i] = (uint16_t)VP(10 + i);
    wsl->head_vpn = list_v[0];
    wsl->page_count = (uint32_t)n;
    for (i = 0; i < n; i++) {
        mmape_t *p = MMAPE_FOR_VPN(list_v[i]);
        p->wsl_index = 7;
        p->flags1 = MMAPE_FLAG1_IN_WSL;
        p->segment = 1;
        p->seg_offset = (uint8_t)i;
        p->prev_vpn = list_v[(i + 1) % n];
        p->next_vpn = list_v[(i - 1 + n) % n];
        PMAP_SEGMAP_ROW(1)[i].flags = PMAP_SEGMAP_INSTALLED | 0x80;
    }
}

static void reset_module(void)
{
    int s;

    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(pft_store, 0, sizeof(pft_store));
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    memset(AST_$AOT.aste, 0, sizeof(AST_$AOT.aste));
    memset(aote_store, 0, sizeof(aote_store));
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    for (s = 1; s <= TEST_SEGMENTS; s++) {
        MMAP_$SEG_ASTE_FOR(s)->aote = &aote_store[s - 1];
    }
    remove_calls = 0;
    TIME_$CLOCKH = 0x1234;
    MMAP_$PAGEABLE_PAGES = 100;
    MMAP_$WSL[7].owner = 0x77;
    MMAP_$WSL[7].scan_pos = 0x88;
}

TEST(trims_two_pages_to_pools)
{
    reset_module();
    build_list(4);
    /* page 10 dirty and on disk, object modified remotely -> pool 4 */
    MMAPE_FOR_VPN(VP(10))->flags2 = MMAPE_FLAG2_MODIFIED | MMAPE_FLAG2_ON_DISK;
    aote_store[0].dtm_high = 0x00010000;

    mmap_$trim_wsl(7, 2);

    ASSERT_EQ(2, MMAP_$WSL[7].page_count);
    ASSERT_EQ(VP(12), MMAP_$WSL[7].head_vpn);
    ASSERT_EQ(VP(13), MMAPE_FOR_VPN(VP(12))->prev_vpn);
    ASSERT_EQ(VP(12), MMAPE_FOR_VPN(VP(13))->prev_vpn);
    ASSERT_EQ(VP(12), MMAPE_FOR_VPN(VP(13))->next_vpn);
    ASSERT_EQ(VP(13), MMAPE_FOR_VPN(VP(12))->next_vpn);

    ASSERT_EQ(2, remove_calls);
    ASSERT_EQ(VP(10), remove_ppn[0]);
    ASSERT_EQ(VP(11), remove_ppn[1]);
    ASSERT_EQ(0x80, PMAP_SEGMAP_ROW(1)[0].flags);    /* bit 5 cleared, bit 7 kept */
    ASSERT_EQ(0x80, PMAP_SEGMAP_ROW(1)[1].flags);
    ASSERT_EQ(0xA0, PMAP_SEGMAP_ROW(1)[2].flags);

    /* collected list is 11 then 10 */
    ASSERT_EQ(1, MMAP_$WSL[MMAP_WSL_POOL_IMPURE].page_count);
    ASSERT_EQ(VP(11), MMAP_$WSL[MMAP_WSL_POOL_IMPURE].head_vpn);
    ASSERT_EQ(1, MMAP_$WSL[MMAP_WSL_POOL_DIRTY_RMT].page_count);
    ASSERT_EQ(VP(10), MMAP_$WSL[MMAP_WSL_POOL_DIRTY_RMT].head_vpn);
    ASSERT_EQ(4, MMAPE_FOR_VPN(VP(10))->wsl_index);
    ASSERT_EQ(100, MMAP_$PAGEABLE_PAGES);       /* -2 unlinked, +2 pooled */
    ASSERT_EQ(0x77, MMAP_$WSL[7].owner);
}

TEST(wired_page_is_dropped_not_pooled)
{
    reset_module();
    build_list(4);
    MMAPE_FOR_VPN(VP(10))->wire_count = 1;

    mmap_$trim_wsl(7, 1);

    ASSERT_EQ(3, MMAP_$WSL[7].page_count);
    ASSERT_EQ(VP(11), MMAP_$WSL[7].head_vpn);
    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(0xA0, PMAP_SEGMAP_ROW(1)[0].flags);
    ASSERT_EQ(0, MMAPE_FOR_VPN(VP(10))->flags1 & MMAPE_FLAG1_IN_WSL);
    ASSERT_EQ(0, MMAP_$WSL[MMAP_WSL_POOL_IMPURE].page_count);
    ASSERT_EQ(99, MMAP_$PAGEABLE_PAGES);
}

TEST(purge_clears_owner_and_stamps_clock)
{
    reset_module();
    build_list(4);
    MMAPE_FOR_VPN(VP(12))->wire_count = 1;
    MMAPE_FOR_VPN(VP(13))->flags1 |= MMAPE_FLAG1_IMPURE;   /* -> pool 1 */

    mmap_$trim_wsl(7, MMAP_TRIM_PURGE_ALL);

    ASSERT_EQ(0, MMAP_$WSL[7].page_count);
    ASSERT_EQ(0, MMAP_$WSL[7].owner);
    ASSERT_EQ(0x88, MMAP_$WSL[7].scan_pos);      /* untouched */
    ASSERT_EQ(0x1234, MMAP_$WSL[7].ws_timestamp);
    ASSERT_EQ(3, remove_calls);
    ASSERT_EQ(2, MMAP_$WSL[MMAP_WSL_POOL_IMPURE].page_count);
    ASSERT_EQ(1, MMAP_$WSL[MMAP_WSL_POOL_PURE].page_count);
    ASSERT_EQ(VP(13), MMAP_$WSL[MMAP_WSL_POOL_PURE].head_vpn);
    ASSERT_EQ(99, MMAP_$PAGEABLE_PAGES);         /* -4 + 3 */
}

TEST(referenced_pages_skipped_inside_window)
{
    reset_module();
    build_list(40);
    PMAPE_FOR_VPN(VP(10))[1] = PMAPE_FLAG_REFERENCED;

    mmap_$trim_wsl(7, 2);

    ASSERT_EQ(38, MMAP_$WSL[7].page_count);
    ASSERT_EQ(VP(13), MMAP_$WSL[7].head_vpn);
    ASSERT_EQ(7, MMAPE_FOR_VPN(VP(10))->wsl_index);
    /* 11 and 12 were re-pooled (add_to_wsl sets IN_WSL again) */
    ASSERT_EQ(MMAP_WSL_POOL_IMPURE, MMAPE_FOR_VPN(VP(11))->wsl_index);
    ASSERT_EQ(MMAP_WSL_POOL_IMPURE, MMAPE_FOR_VPN(VP(12))->wsl_index);
    ASSERT_EQ(VP(13), MMAPE_FOR_VPN(VP(10))->prev_vpn);   /* relinked around 11,12 */
    ASSERT_EQ(VP(10), MMAPE_FOR_VPN(VP(13))->next_vpn);
    ASSERT_EQ(2, remove_calls);
}

TEST(no_skip_window_when_request_is_close_to_size)
{
    reset_module();
    build_list(4);
    PMAPE_FOR_VPN(VP(10))[1] = PMAPE_FLAG_REFERENCED;   /* 2 + 0x20 >= 4: no window */

    mmap_$trim_wsl(7, 2);

    ASSERT_EQ(2, MMAP_$WSL[7].page_count);
    ASSERT_EQ(MMAP_WSL_POOL_IMPURE, MMAPE_FOR_VPN(VP(10))->wsl_index);
}

int main(void)
{
    printf("mmap_$trim_wsl tests\n");
    RUN_TEST(trims_two_pages_to_pools);
    RUN_TEST(wired_page_is_dropped_not_pooled);
    RUN_TEST(purge_clears_owner_and_stamps_clock);
    RUN_TEST(referenced_pages_skipped_inside_window);
    RUN_TEST(no_skip_window_when_request_is_close_to_size);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
