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


uint32_t MMU_$SYSTEM_REV = 0;

/* Simulated MMU hardware registers. */
static volatile uint16_t hw_csr;
static volatile uint16_t hw_power;
static volatile uint8_t hw_status;
static volatile uint8_t hw_mcr_010;
static volatile uint8_t hw_mcr_mask;
static volatile uint8_t hw_mcr_020;
static volatile uint8_t hw_rev;

/* Simulated PFT (MMU_$PTTX is the real block; 4 KB of PPN space is plenty). */
static uint32_t pft_store[0x1000];

static void reset_globals(void)
{

    M68020 = 0;
    VA_TO_PTT_OFFSET_MASK = 0x0FFC00;
    MMU_$VA_SHIFT = 3;
    MMU_$PTT_SHIFT = 8;
    MCR_SHADOW = 0;
    hw_mcr_010 = 0;
    hw_mcr_mask = 0;
    hw_mcr_020 = 0;

    for (int i = 0; i < 0x1000; i++) {
        pft_store[i] = 0;
        MMU_$PTTX.entry[i] = 0;
    }
}

/*
 * MMU_$INIT (0xE23D38) is hand-written assembly - it saves A5 by hand,
 * addresses its data block PC-relative and patches CACHE_$CLEAR's first
 * word with its own `rts' opcode - so it lives in mmu/sau2/init.s and is
 * not host-testable here.
 */

/*
 * "tst.w M68020" (MMU_$INIT) sees the whole word, while "move.b M68020,Dn"
 * (MMU_$PTOV, MMU_$MCR_CHANGE) only sees the high byte.  A value living in
 * the low byte alone must therefore look like a 68020 to the word test and
 * like a 68010 to the byte test.
 */
static void test_flag_word_vs_byte_test(void)
{
    reset_globals();
    M68020 = 0x00FF;
    ASSERT_EQ(1, M68020_IS_020_W() ? 1 : 0);
    ASSERT_EQ(0, M68020_IS_020_B() ? 1 : 0);

    M68020 = 0xFF00;
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
    MMU_$PTTX.entry[4] = 0x0030;

    M68020 = 0xFF00;
    ASSERT_EQ(((0x00050000u | 0x0030u) << 6) & 0xFFFFFFFFu, MMU_$PTOV(4));

    M68020 = 0x0000;
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

    M68020 = 0xFF00;
    MMU_$MCR_CHANGE(3);                 /* 0xB - 3 = 8, & 7 = 0 */
    ASSERT_EQ(0x01, hw_mcr_020);
    MMU_$MCR_CHANGE(3);
    ASSERT_EQ(0x00, hw_mcr_020);

    M68020 = 0x0000;
    hw_mcr_mask = 0x01;
    MMU_$MCR_CHANGE(5);
    ASSERT_EQ(0x20, MCR_SHADOW);
    ASSERT_EQ(0x21, hw_mcr_010);
}

int main(void)
{
    printf("Running MMU global-layout tests...\n\n");

    RUN_TEST(flag_word_vs_byte_test);
    RUN_TEST(ptov_uses_high_byte_flag);
    RUN_TEST(mcr_change_paths);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

/* The real implementations under test. */
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
