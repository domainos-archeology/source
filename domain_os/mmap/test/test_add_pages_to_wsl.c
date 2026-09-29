/*
 * mmap/test/test_add_pages_to_wsl.c - Unit tests for mmap_$add_pages_to_wsl
 * (0x00E0C5AE), from mmap/internal.c
 *
 * The fix under test is the splice into a non-empty list
 * (0x00E0C690-0x00E0C6C6): head.next_vpn = LAST, tail.prev_vpn = FIRST,
 * first.next_vpn = tail, last.prev_vpn = head - the run lands at the end of
 * the prev_vpn walk, the same place a single at_tail add lands.
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

#define TEST_PAGES 64
static mmape_t  mmape_store[TEST_PAGES];
static uint32_t pft_store[TEST_PAGES];
mmap_globals_t MMAP_GLOBALS_STORAGE;
mmape_t  *mmap_mmape_base = mmape_store;
uint32_t *mmu_pft_base    = pft_store;
aste_t MMAP_$SEG_ASTE[4];
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
uint32_t TIME_$CLOCKH;

void MMU_$REMOVE(uint32_t ppn) { (void)ppn; }

#include "../internal.c"

static void reset_module(void)
{
    memset(mmape_store, 0, sizeof(mmape_store));
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    MMAP_$PAGEABLE_PAGES = 100;
}

/* walk prev_vpn from the head, n steps, returning the vpn reached */
static uint32_t walk(uint16_t wsl, int n)
{
    uint32_t v = MMAP_$WSL[wsl].head_vpn;
    while (n-- > 0) v = MMAPE_FOR_VPN(v)->prev_vpn;
    return v;
}

TEST(three_pages_into_empty_list)
{
    uint32_t arr[3] = { 10, 11, 12 };

    reset_module();
    MMAPE_FOR_VPN(11)->priority = 0x55;

    mmap_$add_pages_to_wsl(arr, 3, 7);

    ASSERT_EQ(3, MMAP_$WSL[7].page_count);
    ASSERT_EQ(10, MMAP_$WSL[7].head_vpn);
    ASSERT_EQ(11, walk(7, 1));
    ASSERT_EQ(12, walk(7, 2));
    ASSERT_EQ(10, walk(7, 3));
    ASSERT_EQ(12, MMAPE_FOR_VPN(10)->next_vpn);   /* tail */
    ASSERT_EQ(10, MMAPE_FOR_VPN(11)->next_vpn);
    ASSERT_EQ(11, MMAPE_FOR_VPN(12)->next_vpn);
    ASSERT_EQ(7, MMAPE_FOR_VPN(11)->wsl_index);
    ASSERT_EQ(0, MMAPE_FOR_VPN(11)->priority);
    ASSERT_EQ(MMAPE_FLAG1_IN_WSL, MMAPE_FOR_VPN(12)->flags1);
    ASSERT_EQ(103, MMAP_$PAGEABLE_PAGES);
}

TEST(run_spliced_at_tail_of_populated_list)
{
    uint32_t arr[3] = { 10, 11, 12 };
    mmape_t *p20 = MMAPE_FOR_VPN(20);

    reset_module();
    MMAP_$WSL[7].page_count = 1;
    MMAP_$WSL[7].head_vpn = 20;
    p20->prev_vpn = 20;
    p20->next_vpn = 20;

    mmap_$add_pages_to_wsl(arr, 3, 7);

    ASSERT_EQ(4, MMAP_$WSL[7].page_count);
    ASSERT_EQ(20, MMAP_$WSL[7].head_vpn);
    ASSERT_EQ(10, walk(7, 1));
    ASSERT_EQ(11, walk(7, 2));
    ASSERT_EQ(12, walk(7, 3));
    ASSERT_EQ(20, walk(7, 4));
    ASSERT_EQ(12, p20->next_vpn);                  /* head.next = last */
    ASSERT_EQ(20, MMAPE_FOR_VPN(10)->next_vpn);   /* first.next = tail */
    ASSERT_EQ(20, MMAPE_FOR_VPN(12)->prev_vpn);   /* last.prev = head */
}

TEST(single_page_links_to_itself)
{
    uint32_t arr[1] = { 10 };

    reset_module();

    mmap_$add_pages_to_wsl(arr, 1, 7);

    ASSERT_EQ(1, MMAP_$WSL[7].page_count);
    ASSERT_EQ(10, MMAP_$WSL[7].head_vpn);
    ASSERT_EQ(10, MMAPE_FOR_VPN(10)->next_vpn);
    ASSERT_EQ(10, MMAPE_FOR_VPN(10)->prev_vpn);
}

TEST(two_pages_skip_middle_loop)
{
    uint32_t arr[2] = { 10, 11 };

    reset_module();

    mmap_$add_pages_to_wsl(arr, 2, 7);

    ASSERT_EQ(2, MMAP_$WSL[7].page_count);
    ASSERT_EQ(11, MMAPE_FOR_VPN(10)->prev_vpn);
    ASSERT_EQ(11, MMAPE_FOR_VPN(10)->next_vpn);
    ASSERT_EQ(10, MMAPE_FOR_VPN(11)->next_vpn);
    ASSERT_EQ(10, MMAPE_FOR_VPN(11)->prev_vpn);
}

int main(void)
{
    printf("mmap_$add_pages_to_wsl tests\n");
    RUN_TEST(three_pages_into_empty_list);
    RUN_TEST(run_spliced_at_tail_of_populated_list);
    RUN_TEST(single_page_links_to_itself);
    RUN_TEST(two_pages_skip_middle_loop);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
