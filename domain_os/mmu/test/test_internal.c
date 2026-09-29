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

uint32_t MMU_$SYSTEM_REV;

static uint32_t pft_store[0x1000];
static uint16_t ptt_store[0x4000];

/* The MMU module's data blocks (MMU_$GLOBALS with the image's contents,
 * MMU_$PTTX), and MCR_SHADOW, which mmu/sau2/mcr_change.s defines on the
 * target. */
#include "../mmu_data.c"
uint8_t MCR_SHADOW;

/* The SAU2 MMU hardware (arch/m68k/sau2/hw.h) as this test's own cells. */
#define SAU2_MMU_CSR            (&hw_csr)
#define SAU2_MMU_POWER_REG      (&hw_power)
#define SAU2_MMU_FPU_OWNER_REG  (&hw_fpu_owner)
#define SAU2_MMU_STATUS_REG     (&hw_status)
#define SAU2_MMU_MCR_M68010     (&hw_mcr_010)
#define SAU2_MMU_MCR_MASK       (&hw_mcr_mask)
#define SAU2_MMU_MCR_M68020     (&hw_mcr_020)
#define SAU2_MMU_HW_REV         (&hw_rev)
#define SAU2_PFT_BASE           pft_store
#define SAU2_PTT_BASE           ptt_store

#include "../internal.c"

/* chain 5 -> 7 -> 9 -> 5, hanging off PTT entry 0x80 (MMU_$PTTX[*] = 4,
 * 4 << 6 = 0x100 bytes = word index 0x80) */
static void reset(void)
{
    memset(pft_store, 0, sizeof pft_store);
    memset(MMU_$PTTX.entry, 0, sizeof MMU_$PTTX.entry);
    memset(ptt_store, 0, sizeof ptt_store);
    pft_store[5] = 0xAAAA8007u;     /* head bit, link 7 */
    pft_store[7] = 0xBBBB7009u;     /* mod+ref+global, link 9 */
    pft_store[9] = 0xCCCC2005u;     /* ref, link back to 5 */
    MMU_$PTTX.entry[5] = MMU_$PTTX.entry[7] = MMU_$PTTX.entry[9] = 4;
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
    MMU_$PTTX.entry[3] = 2;              /* PTT word index 0x40 */
    ptt_store[0x40] = 3;
    mmu_$remove_pmape(3);
    ASSERT_EQ(0, ptt_store[0x40]);
    ASSERT_EQ(0x00000000u, pft_store[3]);
}

TEST(unlinked_entry_untouched)
{
    reset();
    pft_store[2] = 0x5555E000u;     /* link 0 */
    MMU_$PTTX.entry[2] = 4;
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
