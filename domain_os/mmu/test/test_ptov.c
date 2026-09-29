/*
 * mmu/test/test_ptov.c - Unit tests for MMU_$PTOV (0x00E241B8),
 * MMU_$MCR_CHANGE (0x00E242A0), MMU_$NORMAL_MODE (0x00E24280),
 * MMU_$POWER_OFF (0x00E2428C) and MMU_$INSTALL_ASID (0x00E24204)
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
uint16_t PROC1_$AS_ID;
uint16_t FP_$OWNER;

static volatile uint16_t hw_csr, hw_power;
static volatile uint8_t hw_status, hw_mcr_010, hw_mcr_mask, hw_mcr_020;
static volatile uint8_t hw_fpu_owner;    /* the first byte of the power register, 0xFFB402 */
static uint32_t pft_store[0x1000];
static int cache_clears;

uint32_t CACHE_$CLEAR(void) { cache_clears++; return 0; }

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

#include "../ptov.c"
#include "../mcr_change.c"
#include "../normal_mode.c"
#include "../power_off.c"
#include "../install_asid.c"

static void reset(void)
{
    memset(pft_store, 0, sizeof pft_store);
    memset(MMU_$PTTX.entry, 0, sizeof MMU_$PTTX.entry);
    hw_csr = hw_power = 0;
    hw_status = hw_mcr_010 = hw_mcr_mask = hw_mcr_020 = 0;
    hw_fpu_owner = 0;
    MCR_SHADOW = 0;
    M68020 = 0x0100;
    cache_clears = 0;
}

TEST(ptov_unmapped_is_zero)
{
    reset();
    pft_store[0x40] = 0xFFFFF000u;
    ASSERT_EQ(0, MMU_$PTOV(0x40));
}

TEST(ptov_68020)
{
    reset();
    pft_store[0x40] = 0x000A0005u;
    MMU_$PTTX.entry[0x40] = 0x1234;
    ASSERT_EQ((0x000A0000u | 0x1234u) << 6, MMU_$PTOV(0x40));
}

/* 68010: the low word is shifted left 2 AS A WORD before the << 4 */
TEST(ptov_68010_word_shift)
{
    reset();
    M68020 = 0;
    pft_store[0x40] = 0x00050001u;
    MMU_$PTTX.entry[0x40] = 0xC001;
    ASSERT_EQ((0x00050000u | ((0xC001u << 2) & 0xFFFF)) << 4, MMU_$PTOV(0x40));
}

TEST(mcr_change_68020_inverts_bit)
{
    reset();
    MMU_$MCR_CHANGE(3);                 /* 0xb - 3 = 8 -> bit 0 */
    ASSERT_EQ(0x01, hw_mcr_020);
    MMU_$MCR_CHANGE(3);
    ASSERT_EQ(0x00, hw_mcr_020);
    MMU_$MCR_CHANGE(0x0B);              /* bit 0 again */
    ASSERT_EQ(0x01, hw_mcr_020);
}

TEST(mcr_change_68010_shadow)
{
    reset();
    M68020 = 0x0001;                /* low byte only: still "68010" */
    hw_mcr_mask = 0xFF;
    MMU_$MCR_CHANGE(6);
    ASSERT_EQ(0x40, MCR_SHADOW);
    ASSERT_EQ(0x41, hw_mcr_010);
    MMU_$MCR_CHANGE(9);                 /* 9 & 7 = 1 */
    ASSERT_EQ(0x42, MCR_SHADOW);
}

TEST(normal_mode_and_power_off)
{
    reset();
    ASSERT_EQ(0, MMU_$NORMAL_MODE());
    hw_status = 0x10;
    ASSERT_EQ(0xFF, (uint8_t)MMU_$NORMAL_MODE());
    ASSERT_EQ(0, MMU_$POWER_OFF());
    hw_power = 0x0004;
    ASSERT_EQ(0xFF, (uint8_t)MMU_$POWER_OFF());
    hw_power = 0x0400;                  /* bit 2 of the WORD only */
    ASSERT_EQ(0, MMU_$POWER_OFF());
}

TEST(install_asid_high_byte_and_cache)
{
    reset();
    MMU_$PID_PRIV = 0x0303;
    FP_$OWNER = 0x1234;
    MMU_$INSTALL_ASID(0x0142);
    ASSERT_EQ(0x0142, PROC1_$AS_ID);
    ASSERT_EQ(0x4203, MMU_$PID_PRIV);
    ASSERT_EQ(0x4203, hw_csr);
    ASSERT_EQ(0x34, hw_fpu_owner);      /* a byte store into 0xFFB402 */
    ASSERT_EQ(0, hw_power);             /* no word read-modify-write */
    ASSERT_EQ(1, cache_clears);
}

int main(void)
{
    printf("test_ptov:\n");
    RUN_TEST(ptov_unmapped_is_zero);
    RUN_TEST(ptov_68020);
    RUN_TEST(ptov_68010_word_shift);
    RUN_TEST(mcr_change_68020_inverts_bit);
    RUN_TEST(mcr_change_68010_shadow);
    RUN_TEST(normal_mode_and_power_off);
    RUN_TEST(install_asid_high_byte_and_cache);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
