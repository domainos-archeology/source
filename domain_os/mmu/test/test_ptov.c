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
uint16_t PROC1_$AS_ID;
uint16_t FP_$OWNER;

static volatile uint16_t hw_csr, hw_power;
static volatile uint8_t hw_status, hw_mcr_010, hw_mcr_mask, hw_mcr_020;
static uint32_t pft_store[0x1000];
static uint16_t asid_store[0x1000];
static int cache_clears;

uint32_t CACHE_$CLEAR(void) { cache_clears++; return 0; }

#include "../ptov.c"
#include "../mcr_change.c"
#include "../normal_mode.c"
#include "../power_off.c"
#include "../install_asid.c"

static void reset(void)
{
    mmu_pft_base = pft_store;
    mmu_asid_table_base = asid_store;
    mmu_csr = &hw_csr;
    mmu_power_reg = &hw_power;
    mmu_status_reg = &hw_status;
    mmu_mcr_m68010 = &hw_mcr_010;
    mmu_mcr_mask = &hw_mcr_mask;
    mmu_mcr_m68020 = &hw_mcr_020;
    memset(pft_store, 0, sizeof pft_store);
    memset(asid_store, 0, sizeof asid_store);
    hw_csr = hw_power = 0;
    hw_status = hw_mcr_010 = hw_mcr_mask = hw_mcr_020 = 0;
    mmu_mcr_shadow = 0;
    mmu_m68020 = 0x0100;
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
    asid_store[0x40] = 0x1234;
    ASSERT_EQ((0x000A0000u | 0x1234u) << 6, MMU_$PTOV(0x40));
}

/* 68010: the low word is shifted left 2 AS A WORD before the << 4 */
TEST(ptov_68010_word_shift)
{
    reset();
    mmu_m68020 = 0;
    pft_store[0x40] = 0x00050001u;
    asid_store[0x40] = 0xC001;
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
    mmu_m68020 = 0x0001;                /* low byte only: still "68010" */
    hw_mcr_mask = 0xFF;
    MMU_$MCR_CHANGE(6);
    ASSERT_EQ(0x40, mmu_mcr_shadow);
    ASSERT_EQ(0x41, hw_mcr_010);
    MMU_$MCR_CHANGE(9);                 /* 9 & 7 = 1 */
    ASSERT_EQ(0x42, mmu_mcr_shadow);
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
    mmu_pid_priv = 0x0303;
    FP_$OWNER = 0x1234;
    MMU_$INSTALL_ASID(0x0142);
    ASSERT_EQ(0x0142, PROC1_$AS_ID);
    ASSERT_EQ(0x4203, mmu_pid_priv);
    ASSERT_EQ(0x4203, hw_csr);
    ASSERT_EQ(0x34, (uint8_t)hw_power);
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
