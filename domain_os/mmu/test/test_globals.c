/*
 * mmu/test/test_globals.c - Unit tests for the recovered MMU module globals
 *
 * Covers bead source-llz: the M68020 flag lives at 0xE23D2E (not 0xE23D2A),
 * VA_TO_PTT_OFFSET_MASK at 0xE23D30, MMU_$VA_SHIFT at 0xE23D34 and
 * MMU_$PTT_SHIFT at 0xE23D36.  The shift counts used to be read as a byte at
 * "&VA_TO_PTT_OFFSET_MASK + 4" through the wrong base, which produced a
 * garbage shift; these tests pin the values the assembly actually uses.
 *
 * The real .c files are #included at the bottom so the real functions run.
 */

#include <stdio.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mmu/mmu_internal.h"

/* Host-side backing store for the module globals (see mmu/mmu_data.c). */
uint16_t *mmu_ptt_base = (void *)0;
uint32_t *mmu_pft_base = (void *)0;
uint16_t *mmu_asid_table_base = (void *)0;
volatile uint16_t *mmu_csr = (void *)0;
volatile uint16_t *mmu_power_reg = (void *)0;
volatile uint8_t *mmu_status_reg = (void *)0;
volatile uint8_t *mmu_mcr_m68010 = (void *)0;
volatile uint8_t *mmu_mcr_mask = (void *)0;
volatile uint8_t *mmu_mcr_m68020 = (void *)0;
volatile uint8_t *mmu_hw_rev = (void *)0;

uint16_t mmu_m68020 = 0;
uint16_t mmu_pid_priv = 0;
uint32_t mmu_va_to_ptt_mask = 0x0FFC00;
uint16_t mmu_va_shift = 3;
uint16_t mmu_ptt_shift = 8;
uint8_t mmu_sysrev = 0;
uint16_t mmu_current_asid = 0;
uint8_t mmu_mcr_shadow = 0;
uint32_t MMU_$SYSTEM_REV = 0;

/* Simulated MMU hardware registers. */
static volatile uint16_t hw_csr;
static volatile uint16_t hw_power;
static volatile uint8_t hw_status;
static volatile uint8_t hw_mcr_010;
static volatile uint8_t hw_mcr_mask;
static volatile uint8_t hw_mcr_020;
static volatile uint8_t hw_rev;

/* Simulated PFT / ASID tables (4 KB of PPN space is plenty). */
static uint32_t pft_store[0x1000];
static uint16_t asid_store[0x1000];

static void reset_globals(void)
{
    mmu_pft_base = pft_store;
    mmu_asid_table_base = asid_store;
    mmu_csr = &hw_csr;
    mmu_power_reg = &hw_power;
    mmu_status_reg = &hw_status;
    mmu_mcr_m68010 = &hw_mcr_010;
    mmu_mcr_mask = &hw_mcr_mask;
    mmu_mcr_m68020 = &hw_mcr_020;
    mmu_hw_rev = &hw_rev;

    mmu_m68020 = 0;
    mmu_va_to_ptt_mask = 0x0FFC00;
    mmu_va_shift = 3;
    mmu_ptt_shift = 8;
    mmu_mcr_shadow = 0;
    hw_mcr_010 = 0;
    hw_mcr_mask = 0;
    hw_mcr_020 = 0;

    for (int i = 0; i < 0x1000; i++) {
        pft_store[i] = 0;
        asid_store[i] = 0;
    }
}

/*
 * MMU_$INIT (0xE23D38): on a 68020 it writes the mask and BOTH shift words.
 * The shift words are separate 16-bit globals, not bytes inside the mask.
 */
static void test_init_sets_020_shifts(void)
{
    reset_globals();
    mmu_m68020 = 0xFF00;        /* boolean lives in the high byte */
    MMU_$INIT();
    ASSERT_EQ(0x3FFC00, VA_TO_PTT_OFFSET_MASK);
    ASSERT_EQ(1, MMU_$VA_SHIFT);
    ASSERT_EQ(6, MMU_$PTT_SHIFT);
}

/* On a 68010 MMU_$INIT leaves the shipped defaults alone. */
static void test_init_leaves_010_defaults(void)
{
    reset_globals();
    mmu_m68020 = 0;
    MMU_$INIT();
    ASSERT_EQ(0x0FFC00, VA_TO_PTT_OFFSET_MASK);
    ASSERT_EQ(3, MMU_$VA_SHIFT);
    ASSERT_EQ(8, MMU_$PTT_SHIFT);
}

/*
 * "tst.w M68020" (MMU_$INIT) sees the whole word, while "move.b M68020,Dn"
 * (MMU_$PTOV, MMU_$MCR_CHANGE) only sees the high byte.  A value living in
 * the low byte alone must therefore look like a 68020 to the word test and
 * like a 68010 to the byte test.
 */
static void test_flag_word_vs_byte_test(void)
{
    reset_globals();
    mmu_m68020 = 0x00FF;
    ASSERT_EQ(1, M68020_IS_020_W() ? 1 : 0);
    ASSERT_EQ(0, M68020_IS_020_B() ? 1 : 0);

    mmu_m68020 = 0xFF00;
    ASSERT_EQ(1, M68020_IS_020_W() ? 1 : 0);
    ASSERT_EQ(1, M68020_IS_020_B() ? 1 : 0);
}

/*
 * MMU_$PTOV (0xE241B8) selects its shift from the HIGH byte of M68020:
 *   68020+ : result <<= 6      (0xE241E6 lsl.l #6)
 *   68010  : (low word <<= 2) then result <<= 4  (0xE241EA/0xE241EC)
 */
static void test_ptov_uses_high_byte_flag(void)
{
    reset_globals();
    /* PPN 4: PMAPE low word must be a non-zero link, high nibble supplies
     * bits 16-19 of the result. */
    pft_store[4] = 0x00050001;
    asid_store[4] = 0x0030;

    mmu_m68020 = 0xFF00;
    ASSERT_EQ(((0x00050000u | 0x0030u) << 6) & 0xFFFFFFFFu, MMU_$PTOV(4));

    mmu_m68020 = 0x0000;
    {
        uint32_t r = (0x00050000u & 0x000F0000u) | 0x0030u;
        r = ((r & 0xFFFF0000u) | ((r & 0xFFFFu) << 2)) << 4;
        ASSERT_EQ(r, MMU_$PTOV(4));
    }

    /* A zero link word means "no mapping". */
    pft_store[4] = 0x00050000;
    ASSERT_EQ(0, MMU_$PTOV(4));
}

/*
 * MMU_$MCR_CHANGE (0xE242A0) also branches on the HIGH byte.
 * 68020+ : bchg.b (0xB - bit) in the 0xFFB408 register.
 * 68010  : toggle bit in the shadow, then write shadow | (mask & 1).
 */
static void test_mcr_change_paths(void)
{
    reset_globals();

    mmu_m68020 = 0xFF00;
    MMU_$MCR_CHANGE(3);                 /* 0xB - 3 = 8, & 7 = 0 */
    ASSERT_EQ(0x01, hw_mcr_020);
    MMU_$MCR_CHANGE(3);
    ASSERT_EQ(0x00, hw_mcr_020);

    mmu_m68020 = 0x0000;
    hw_mcr_mask = 0x01;
    MMU_$MCR_CHANGE(5);
    ASSERT_EQ(0x20, mmu_mcr_shadow);
    ASSERT_EQ(0x21, hw_mcr_010);
}

int main(void)
{
    printf("Running MMU global-layout tests...\n\n");

    RUN_TEST(init_sets_020_shifts);
    RUN_TEST(init_leaves_010_defaults);
    RUN_TEST(flag_word_vs_byte_test);
    RUN_TEST(ptov_uses_high_byte_flag);
    RUN_TEST(mcr_change_paths);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

/* The real implementations under test. */
#include "../init.c"
#include "../ptov.c"
#include "../mcr_change.c"
