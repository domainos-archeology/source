/*
 * mmap/test/test_get_impure.c - Unit tests for MMAP_$GET_IMPURE (0x00E0D5EA)
 *
 * The fix under test: D5.b, the "take this page" flag, is loaded from the
 * all_pages argument at 0x00E0D654 on every iteration, so with all_pages
 * true even an ON_DISK page or one whose object lacks attribute bit 12 is
 * returned.  Also covers the 100-page scan cap when all_pages is false.
 */

#include <stdio.h>
#include <string.h>

#include "mmap/mmap_internal.h"
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

#define TEST_PAGES    256
#define TEST_SEGMENTS 4
static mmape_t  mmape_store[TEST_PAGES];
static uint32_t pft_store[TEST_PAGES];
mmap_globals_t MMAP_GLOBALS_STORAGE;
mmape_t  *mmap_mmape_base = mmape_store;
uint32_t *mmu_pft_base    = pft_store;
aste_t MMAP_$SEG_ASTE[TEST_SEGMENTS];
static aote_t aote_store[TEST_SEGMENTS];

#include "../get_impure.c"

static uint16_t list_v[TEST_PAGES];

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
        p->flags2 = MMAPE_FLAG2_MODIFIED;
        p->prev_vpn = v[(i + 1) % n];
        p->next_vpn = v[(i - 1 + n) % n];
    }
}

static void reset_module(void)
{
    int s;

    memset(mmape_store, 0, sizeof(mmape_store));
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    memset(MMAP_$SEG_ASTE, 0, sizeof(MMAP_$SEG_ASTE));
    memset(aote_store, 0, sizeof(aote_store));
    for (s = 1; s <= TEST_SEGMENTS; s++) {
        MMAP_$SEG_ASTE_FOR(s)->aote = &aote_store[s - 1];
    }
    /* segment 1 has attribute bit 12 set, segment 2 does not */
    aote_store[0].attr_flags_hi = 0x10;
    MMAP_$PAGEABLE_PAGES = 500;
}

/* pages 10 (seg 1), 11 (seg 1, ON_DISK), 12 (seg 2) on WSL 7 */
static void three_page_list(void)
{
    static const uint16_t v[] = { 10, 11, 12 };

    build_list(7, v, 3);
    MMAPE_FOR_VPN(10)->segment = 1;
    MMAPE_FOR_VPN(11)->segment = 1;
    MMAPE_FOR_VPN(11)->flags2 |= MMAPE_FLAG2_ON_DISK;
    MMAPE_FOR_VPN(12)->segment = 2;
}

TEST(selective_takes_only_bit12_pages)
{
    uint32_t out[8] = { 0 };
    uint32_t scanned = 99;
    uint16_t returned = 99;

    reset_module();
    three_page_list();

    MMAP_$GET_IMPURE(7, out, false, 10, &scanned, &returned);

    ASSERT_EQ(1, returned);
    ASSERT_EQ(3, scanned);
    ASSERT_EQ(10, out[0]);
    ASSERT_EQ(2, MMAP_$WSL[7].page_count);
    ASSERT_EQ(11, MMAP_$WSL[7].head_vpn);
    ASSERT_EQ(0, MMAPE_FOR_VPN(10)->flags1 & MMAPE_FLAG1_IN_WSL);
    ASSERT_EQ(0, MMAPE_FOR_VPN(10)->flags2 & MMAPE_FLAG2_MODIFIED);
    ASSERT_EQ(12, MMAPE_FOR_VPN(11)->next_vpn);
    ASSERT_EQ(11, MMAPE_FOR_VPN(12)->prev_vpn);
    ASSERT_EQ(499, MMAP_$PAGEABLE_PAGES);
}

TEST(all_pages_takes_everything)
{
    uint32_t out[8] = { 0 };
    uint32_t scanned = 99;
    uint16_t returned = 99;

    reset_module();
    three_page_list();

    MMAP_$GET_IMPURE(7, out, true, 10, &scanned, &returned);

    ASSERT_EQ(3, returned);
    ASSERT_EQ(3, scanned);
    ASSERT_EQ(10, out[0]);
    ASSERT_EQ(11, out[1]);
    ASSERT_EQ(12, out[2]);
    ASSERT_EQ(0, MMAP_$WSL[7].page_count);
    ASSERT_EQ(10, MMAP_$WSL[7].head_vpn);   /* count 0: head not rewritten */
    ASSERT_EQ(497, MMAP_$PAGEABLE_PAGES);
}

TEST(max_pages_caps_the_return)
{
    uint32_t out[8] = { 0 };
    uint32_t scanned = 99;
    uint16_t returned = 99;

    reset_module();
    three_page_list();

    MMAP_$GET_IMPURE(7, out, true, 1, &scanned, &returned);

    ASSERT_EQ(1, returned);
    ASSERT_EQ(1, scanned);
    ASSERT_EQ(2, MMAP_$WSL[7].page_count);
    ASSERT_EQ(11, MMAP_$WSL[7].head_vpn);
}

TEST(selective_scan_capped_at_100)
{
    uint32_t out[8] = { 0 };
    uint32_t scanned = 0;
    uint16_t returned = 0;
    int i;

    reset_module();
    for (i = 0; i < 150; i++) list_v[i] = (uint16_t)(20 + i);
    build_list(7, list_v, 150);
    for (i = 0; i < 150; i++) MMAPE_FOR_VPN(20 + i)->segment = 2;

    MMAP_$GET_IMPURE(7, out, false, 200, &scanned, &returned);

    ASSERT_EQ(0, returned);
    ASSERT_EQ(100, scanned);
    ASSERT_EQ(150, MMAP_$WSL[7].page_count);
    ASSERT_EQ(120, MMAP_$WSL[7].head_vpn);
}

int main(void)
{
    printf("MMAP_$GET_IMPURE tests\n");
    RUN_TEST(selective_takes_only_bit12_pages);
    RUN_TEST(all_pages_takes_everything);
    RUN_TEST(max_pages_caps_the_return);
    RUN_TEST(selective_scan_capped_at_100);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
