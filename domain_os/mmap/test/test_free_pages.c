/*
 * mmap/test/test_free_pages.c - Unit tests for MMAP_$FREE_PAGES (0x00E0CE56)
 *
 * Covers the three fixes of the re-emission: the three-argument frame
 * (pid word never read), no early return when count == 0 (the lock is
 * taken and the free-pool splice still runs), and the last array element's
 * prev_vpn intermediate value being its own vpn before the final splice
 * overwrites it.
 */

#include <stdio.h>
#include <string.h>

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
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

static int lock_calls, unlock_calls, crash_calls;
static status_$t crash_status;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    (void)lockp;
    lock_calls++;
    return 0x1234;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    (void)lockp;
    if (token == 0x1234) unlock_calls++;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
}

#include "../free_pages.c"

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
    memset(mmape_store, 0, sizeof(mmape_store));
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    lock_calls = unlock_calls = crash_calls = 0;
}

TEST(two_pages_into_empty_free_pool)
{
    static const uint16_t ws[] = { 10, 11 };
    uint32_t arr[2] = { 10, 11 };

    reset_module();
    build_list(7, ws, 2);
    MMAPE_FOR_VPN(10)->flags2 = 0xFF;
    MMAPE_FOR_VPN(10)->priority = 0xFF;

    MMAP_$FREE_PAGES(3, arr, 2);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(0, MMAP_$WSL[7].page_count);
    ASSERT_EQ(2, MMAP_$WSL[0].page_count);
    ASSERT_EQ(10, MMAP_$WSL[0].head_vpn);
    ASSERT_EQ(11, MMAPE_FOR_VPN(10)->next_vpn);
    ASSERT_EQ(11, MMAPE_FOR_VPN(10)->prev_vpn);   /* next in array */
    ASSERT_EQ(10, MMAPE_FOR_VPN(11)->prev_vpn);   /* final splice */
    ASSERT_EQ(10, MMAPE_FOR_VPN(11)->next_vpn);   /* prev in array */
    ASSERT_EQ(0, MMAPE_FOR_VPN(10)->wsl_index);
    ASSERT_EQ(0, MMAPE_FOR_VPN(11)->wsl_index);
    ASSERT_EQ(0x3F, MMAPE_FOR_VPN(10)->flags2);   /* andi.w #0xFF3F: low byte */
    ASSERT_EQ(0xFF, MMAPE_FOR_VPN(10)->priority); /* high byte untouched */
}

TEST(single_page_into_populated_free_pool)
{
    static const uint16_t ws[] = { 10 };
    static const uint16_t fp[] = { 20 };
    uint32_t arr[1] = { 10 };

    reset_module();
    build_list(7, ws, 1);
    build_list(0, fp, 1);

    MMAP_$FREE_PAGES(3, arr, 1);

    ASSERT_EQ(2, MMAP_$WSL[0].page_count);
    ASSERT_EQ(20, MMAP_$WSL[0].head_vpn);
    ASSERT_EQ(10, MMAPE_FOR_VPN(20)->next_vpn);
    ASSERT_EQ(10, MMAPE_FOR_VPN(20)->prev_vpn);
    ASSERT_EQ(20, MMAPE_FOR_VPN(10)->next_vpn);
    ASSERT_EQ(20, MMAPE_FOR_VPN(10)->prev_vpn);
    /* the source list lost its only page */
    ASSERT_EQ(0, MMAP_$WSL[7].page_count);
    ASSERT_EQ(10, MMAP_$WSL[7].head_vpn);   /* head = page->prev_vpn (self) */
}

TEST(count_zero_still_locks_and_splices)
{
    static const uint16_t fp[] = { 20 };
    /* arr[0] plays the "vpn_array[-1]" slot the image reads for `last' */
    uint32_t arr[2] = { 30, 31 };

    reset_module();
    build_list(0, fp, 1);

    MMAP_$FREE_PAGES(3, &arr[1], 0);

    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(1, MMAP_$WSL[0].page_count);
    ASSERT_EQ(30, MMAPE_FOR_VPN(20)->next_vpn);   /* head.next = last */
    ASSERT_EQ(31, MMAPE_FOR_VPN(20)->prev_vpn);   /* tail(=head).prev = first */
    ASSERT_EQ(20, MMAPE_FOR_VPN(31)->next_vpn);   /* first.next = tail */
    ASSERT_EQ(20, MMAPE_FOR_VPN(30)->prev_vpn);   /* last.prev = head */
}

TEST(page_not_in_wsl_crashes)
{
    static const uint16_t ws[] = { 10 };
    uint32_t arr[1] = { 10 };

    reset_module();
    build_list(7, ws, 1);
    MMAPE_FOR_VPN(10)->flags1 = 0;

    MMAP_$FREE_PAGES(3, arr, 1);

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(status_$mmap_inconsistent_mmape, crash_status);
}

int main(void)
{
    printf("MMAP_$FREE_PAGES tests\n");
    RUN_TEST(two_pages_into_empty_free_pool);
    RUN_TEST(single_page_into_populated_free_pool);
    RUN_TEST(count_zero_still_locks_and_splices);
    RUN_TEST(page_not_in_wsl_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
