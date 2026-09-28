/*
 * mmu/test/test_internal.c - Unit tests for mmu_$remove_pmape (0x00E23DD8)
 * and mmu_$unlink_from_hash (0x00E23DF4)
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

static uint32_t pft_store[0x1000];
static uint16_t asid_store[0x1000];
static uint16_t ptt_store[0x4000];

#include "../internal.c"

/* chain 5 -> 7 -> 9 -> 5, hanging off PTT entry 0x80 (ASID_TABLE[*] = 4,
 * 4 << 6 = 0x100 bytes = word index 0x80) */
static void reset(void)
{
    mmu_pft_base = pft_store;
    mmu_asid_table_base = asid_store;
    mmu_ptt_base = ptt_store;
    memset(pft_store, 0, sizeof pft_store);
    memset(asid_store, 0, sizeof asid_store);
    memset(ptt_store, 0, sizeof ptt_store);
    pft_store[5] = 0xAAAA8007u;     /* head bit, link 7 */
    pft_store[7] = 0xBBBB7009u;     /* mod+ref+global, link 9 */
    pft_store[9] = 0xCCCC2005u;     /* ref, link back to 5 */
    asid_store[5] = asid_store[7] = asid_store[9] = 4;
    ptt_store[0x80] = 5;
}

TEST(remove_middle_entry)
{
    reset();
    mmu_$remove_pmape(7);
    /* predecessor 5: delta = ((0x8007 & ~0x8000) ^ 0x7009) & 0x8FFF = 0x000E */
    ASSERT_EQ(0xAAAA8009u, pft_store[5]);
    /* PTT now names the predecessor */
    ASSERT_EQ(5, ptt_store[0x80]);
    /* removed entry keeps only bits 13 and 14 */
    ASSERT_EQ(0x00006000u, pft_store[7]);
    ASSERT_EQ(0xCCCC2005u, pft_store[9]);
}

/* Removing the head walks round to the last entry, which becomes the PTT
 * value; its head bit is what the masked XOR leaves. */
TEST(remove_head_entry)
{
    reset();
    mmu_$remove_pmape(5);
    /* pred 9: delta = ((0x2005 & ~0x8000) ^ 0x8007) & 0x8FFF = 0x8002 */
    ASSERT_EQ(0xCCCCA007u, pft_store[9]);
    ASSERT_EQ(9, ptt_store[0x80]);
    ASSERT_EQ(0x00000000u, pft_store[5]);
}

TEST(single_entry_chain)
{
    reset();
    pft_store[3] = 0x11119003u;     /* links to itself */
    asid_store[3] = 2;              /* PTT word index 0x40 */
    ptt_store[0x40] = 3;
    mmu_$remove_pmape(3);
    ASSERT_EQ(0, ptt_store[0x40]);
    ASSERT_EQ(0x00000000u, pft_store[3]);
}

TEST(unlinked_entry_untouched)
{
    reset();
    pft_store[2] = 0x5555E000u;     /* link 0 */
    asid_store[2] = 4;
    mmu_$remove_pmape(2);
    ASSERT_EQ(0x5555E000u, pft_store[2]);
    ASSERT_EQ(5, ptt_store[0x80]);
}

/* A caller-supplied predecessor offset skips the walk. */
TEST(unlink_with_known_predecessor)
{
    reset();
    mmu_$unlink_from_hash(9, 7 << 2, pft_store[9], &ptt_store[0x80], &pft_store[9]);
    /* pred 7: delta = ((0x7009 & ~0x8000) ^ 0x2005) & 0x8FFF = 0x000C */
    ASSERT_EQ(0xBBBB7005u, pft_store[7]);
    ASSERT_EQ(7, ptt_store[0x80]);
    ASSERT_EQ(0x00002000u, pft_store[9]);
}

int main(void)
{
    printf("test_internal:\n");
    RUN_TEST(remove_middle_entry);
    RUN_TEST(remove_head_entry);
    RUN_TEST(single_entry_chain);
    RUN_TEST(unlinked_entry_untouched);
    RUN_TEST(unlink_with_known_predecessor);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
