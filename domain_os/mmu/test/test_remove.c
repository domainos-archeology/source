/*
 * mmu/test/test_remove.c - Unit tests for MMU_$REMOVE (0x00E23D64),
 * MMU_$REMOVE_LIST (0x00E23D92), MMU_$REMOVE_VIRTUAL (0x00E23E38) and
 * MMU_$REMOVE_ASID (0x00E23F0C), driving the host models of the
 * mmu/sau2 routines together with mmu/internal.c.
 */

#include <stdio.h>
#include <string.h>

#include "mmu/mmu_internal.h"

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

int __host_intr_disable_count = 0;

uint16_t *mmu_ptt_base;
uint32_t *mmu_pft_base;
uint16_t *mmu_asid_table_base;
volatile uint16_t *mmu_csr;
volatile uint16_t *mmu_power_reg;
volatile uint8_t *mmu_status_reg;
volatile uint8_t *mmu_mcr_m68010;
volatile uint8_t *mmu_mcr_mask;
volatile uint8_t *mmu_mcr_m68020;
volatile uint8_t *mmu_hw_rev;
uint16_t mmu_m68020;
uint16_t mmu_pid_priv;
uint32_t mmu_va_to_ptt_mask = 0x0FFC00;
uint16_t mmu_va_shift = 3;
uint16_t mmu_ptt_shift = 8;
uint8_t mmu_sysrev;
uint16_t mmu_current_asid;
uint8_t mmu_mcr_shadow;
uint32_t MMU_$SYSTEM_REV;
MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);

static volatile uint16_t hw_csr;
static uint32_t pft_store[0x1000];
static uint16_t asid_store[0x1000];
static uint16_t ptt_store[0x80000];     /* the whole 1 MB PTT window */

#include "../internal.c"
#include "../remove.c"
#include "../remove_list.c"
#include "../remove_virtual.c"
#include "../remove_asid.c"

static void reset(void)
{
    mmu_pft_base = pft_store;
    mmu_asid_table_base = asid_store;
    mmu_ptt_base = ptt_store;
    mmu_csr = &hw_csr;
    memset(pft_store, 0, sizeof pft_store);
    memset(asid_store, 0, sizeof asid_store);
    memset(ptt_store, 0, sizeof ptt_store);
    hw_csr = 0;
    mmu_pid_priv = 0x0500;
    mmu_va_shift = 3;
}

/* a one-entry ring for ppn at PTT word index `pi', ASID_TABLE giving the
 * same PTT entry back (pi * 2 bytes = asid << 6) */
static void ring1(uint16_t ppn, uint16_t pi, uint16_t hi)
{
    pft_store[ppn] = ((uint32_t)hi << 16) | 0x8000u | ppn;
    asid_store[ppn] = (uint16_t)((pi * 2) >> 6);
    ptt_store[pi] = ppn;
}

TEST(remove_single_page)
{
    reset();
    ring1(0x22, 0x40, 0x0A00);
    MMU_$REMOVE(0x22);
    ASSERT_EQ(0, pft_store[0x22]);
    ASSERT_EQ(0, ptt_store[0x40]);
    ASSERT_EQ(0x0500, hw_csr);
}

TEST(remove_list_runs_count_pages)
{
    uint32_t ppns[2] = { 0x22, 0x23 };
    reset();
    ring1(0x22, 0x40, 0x0A00);
    ring1(0x23, 0x60, 0x0A00);
    MMU_$REMOVE_LIST(ppns, 2);
    ASSERT_EQ(0, pft_store[0x22]);
    ASSERT_EQ(0, pft_store[0x23]);
    ASSERT_EQ(0, ptt_store[0x60]);
}

/* va 0x20000, asid 5, VA_SHIFT 3: key = high word of ror7(0x100005) = 0x0A00 */
TEST(remove_virtual_single)
{
    uint32_t out[4] = { 0, 0, 0, 0 };
    uint16_t n = 0xFFFF;
    reset();
    pft_store[9] = 0x0A008009u;
    ptt_store[0x10000] = 9;
    MMU_$REMOVE_VIRTUAL(0x20000, 1, 5, out, &n);
    ASSERT_EQ(1, n);
    ASSERT_EQ(9, out[0]);
    ASSERT_EQ(0, pft_store[9]);
    ASSERT_EQ(0, ptt_store[0x10000]);
    ASSERT_EQ(0x0500, hw_csr);
}

/* ring 5 -> 7 -> 5 at that PTT entry; only 7 belongs to asid 5.  The
 * PTT ends up naming the predecessor (5), whose head bit is 5's ^ 7's. */
TEST(remove_virtual_from_ring_points_ptt_at_predecessor)
{
    uint32_t out[4] = { 0, 0, 0, 0 };
    uint16_t n = 0;
    reset();
    pft_store[5] = 0x0C008007u;     /* asid 6, head, link 7 */
    pft_store[7] = 0x0A000005u;     /* asid 5, link 5 */
    ptt_store[0x10000] = 5;
    MMU_$REMOVE_VIRTUAL(0x20000, 1, 5, out, &n);
    ASSERT_EQ(1, n);
    ASSERT_EQ(7, out[0]);
    /* delta = ((0x8007 & ~0x8000) ^ 0x0005) & 0x8fff = 0x0002 */
    ASSERT_EQ(0x0C008005u, pft_store[5]);
    ASSERT_EQ(5, ptt_store[0x10000]);
    ASSERT_EQ(0, pft_store[7]);
}

/* two pages, the second on the next PTT entry; an unrelated asid's page
 * there is left alone */
TEST(remove_virtual_two_pages)
{
    uint32_t out[4] = { 0, 0, 0, 0 };
    uint16_t n = 0;
    reset();
    pft_store[9] = 0x0A008009u;
    ptt_store[0x10000] = 9;
    pft_store[3] = 0x0C008003u;     /* asid 6 */
    ptt_store[0x10200] = 3;
    MMU_$REMOVE_VIRTUAL(0x20000, 2, 5, out, &n);
    ASSERT_EQ(1, n);
    ASSERT_EQ(9, out[0]);
    ASSERT_EQ(0x0C008003u, pft_store[3]);
}

/* 40 pages: the bracket is dropped after 31, taken again for the rest */
TEST(remove_virtual_groups_of_32)
{
    uint32_t out[64];
    uint16_t n = 0;
    int i;
    reset();
    for (i = 0; i < 40; i++) {
        pft_store[0x100 + i] = 0x0A008000u | (0x100 + i);
        ptt_store[0x10000 + i * 0x200] = (uint16_t)(0x100 + i);
    }
    MMU_$REMOVE_VIRTUAL(0x20000, 40, 5, out, &n);
    ASSERT_EQ(40, n);
    ASSERT_EQ(0x127, out[39]);
    ASSERT_EQ(0, pft_store[0x127]);
    ASSERT_EQ(0, __host_intr_disable_count);
}

/* pages 4..6 are pageable (LPPN 4, HPPN 6); 5 and 6 belong to asid 3 */
TEST(remove_asid_scans_pageable_range)
{
    reset();
    MMAP_$LPPN = 4;
    MMAP_$HPPN = 6;
    ring1(5, 0x20, 0x0600);         /* asid 3 << 9 = 0x0600 in the high word */
    ring1(6, 0x30, 0x0600);
    ring1(7, 0x50, 0x0600);         /* beyond HPPN */
    MMU_$REMOVE_ASID(3);
    ASSERT_EQ(0, pft_store[5]);
    ASSERT_EQ(0, pft_store[6]);
    ASSERT_EQ(0, ptt_store[0x20]);
    ASSERT_EQ(0x06008007u, pft_store[7]);
    ASSERT_EQ(0x0500, hw_csr);
}

TEST(remove_asid_other_asid_untouched)
{
    reset();
    MMAP_$LPPN = 4;
    MMAP_$HPPN = 6;
    ring1(5, 0x20, 0x0800);         /* asid 4 */
    MMU_$REMOVE_ASID(3);
    ASSERT_EQ(0x08008005u, pft_store[5]);
}

int main(void)
{
    printf("test_remove:\n");
    RUN_TEST(remove_single_page);
    RUN_TEST(remove_list_runs_count_pages);
    RUN_TEST(remove_virtual_single);
    RUN_TEST(remove_virtual_from_ring_points_ptt_at_predecessor);
    RUN_TEST(remove_virtual_two_pages);
    RUN_TEST(remove_virtual_groups_of_32);
    RUN_TEST(remove_asid_scans_pageable_range);
    RUN_TEST(remove_asid_other_asid_untouched);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
