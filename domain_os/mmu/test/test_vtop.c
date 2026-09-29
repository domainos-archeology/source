/*
 * mmu/test/test_vtop.c - Unit tests for MMU_$VTOP (0x00E2410E),
 * mmu_$vtop_or_crash (0x00E3190C), MMU_$SET_CSR (0x00E241F4),
 * MMU_$SET_PROT (0x00E2422A) and MMU_$SET_SYSREV (0x00E24272)
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "mmu/mmu_internal.h"
#include "misc/misc.h"

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

static volatile uint16_t hw_csr;
static volatile uint8_t hw_rev;
static uint32_t pft_store[0x1000];
static uint16_t ptt_store[0x80000];
static jmp_buf crash_jmp;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }

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

#include "../vtop.c"
#include "../vtop_or_crash.c"
#include "../set_csr.c"
#include "../set_prot.c"
#include "../set_sysrev.c"

static void reset(void)
{
    memset(pft_store, 0, sizeof pft_store);
    memset(ptt_store, 0, sizeof ptt_store);
    hw_csr = 0;
    MMU_$PID_PRIV = 0x0500;
    PROC1_$AS_ID = 5;               /* key for va 0x20000 = 0x0A00 */
}

TEST(vtop_hit)
{
    status_$t st = 0x99;
    reset();
    ptt_store[0x10000] = 9;
    pft_store[9] = 0x0A008009u;
    ASSERT_EQ(9, MMU_$VTOP(0x20000, &st));
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x0500, hw_csr);
}

TEST(vtop_miss_empty_and_unmatched_ring)
{
    status_$t st = 0;
    reset();
    ASSERT_EQ(0, MMU_$VTOP(0x20000, &st));
    ASSERT_EQ(status_$mmu_miss, st);
    ptt_store[0x10000] = 5;
    pft_store[5] = 0x0C008007u;     /* asid 6 -> 7 */
    pft_store[7] = 0x0C000005u;     /* asid 6 -> 5 */
    st = 0;
    ASSERT_EQ(0, MMU_$VTOP(0x20000, &st));
    ASSERT_EQ(status_$mmu_miss, st);
}

TEST(vtop_second_entry_and_global)
{
    status_$t st = 0;
    reset();
    ptt_store[0x10000] = 5;
    pft_store[5] = 0x0C008007u;     /* asid 6 */
    pft_store[7] = 0x0A000005u;     /* asid 5 */
    ASSERT_EQ(7, MMU_$VTOP(0x20000, &st));
    /* a GLOBAL page of another asid at the same VA bits also hits */
    pft_store[7] = 0x0C001005u;
    ASSERT_EQ(7, MMU_$VTOP(0x20000, &st));
    ASSERT_EQ(0, st);
    /* ... but not if its low VA nibble differs */
    pft_store[7] = 0x0C011005u;
    ASSERT_EQ(0, MMU_$VTOP(0x20000, &st));
}

TEST(vtop_or_crash)
{
    reset();
    ptt_store[0x10000] = 9;
    pft_store[9] = 0x0A008009u;
    ASSERT_EQ(9, mmu_$vtop_or_crash(0x20000));
    crash_status = 0;
    if (setjmp(crash_jmp) == 0) {
        (void)mmu_$vtop_or_crash(0x30000);
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(status_$mmu_miss, crash_status);
}

TEST(set_csr_low_byte_into_high_byte)
{
    reset();
    MMU_$PID_PRIV = 0x0303;
    MMU_$SET_CSR(0x0142);
    ASSERT_EQ(0x4203, MMU_$PID_PRIV);
    ASSERT_EQ(0x4203, hw_csr);
}

TEST(set_prot_high_word_field)
{
    reset();
    pft_store[3] = 0x01F5ABCDu;
    ASSERT_EQ(0x1F, MMU_$SET_PROT(3, 2));
    ASSERT_EQ(0x0025ABCDu, pft_store[3]);
    ASSERT_EQ(0x02, MMU_$SET_PROT(3, 0x1F));
    ASSERT_EQ(0x01F5ABCDu, pft_store[3]);
    ASSERT_EQ(0, __host_intr_disable_count);
}

TEST(set_sysrev_low_byte)
{
    reset();
    hw_rev = 0x42;
    MMU_$SYSTEM_REV = 0xAABBCCDDu;
    MMU_$SET_SYSREV();
    ASSERT_EQ(0xAABBCC42u, MMU_$SYSTEM_REV);
}

int main(void)
{
    printf("test_vtop:\n");
    RUN_TEST(vtop_hit);
    RUN_TEST(vtop_miss_empty_and_unmatched_ring);
    RUN_TEST(vtop_second_entry_and_global);
    RUN_TEST(vtop_or_crash);
    RUN_TEST(set_csr_low_byte_into_high_byte);
    RUN_TEST(set_prot_high_word_field);
    RUN_TEST(set_sysrev_low_byte);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
