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

static volatile uint16_t hw_csr;
static volatile uint8_t hw_rev;
static uint32_t pft_store[0x1000];
static uint16_t ptt_store[0x80000];
static jmp_buf crash_jmp;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }

#include "../vtop.c"
#include "../vtop_or_crash.c"
#include "../set_csr.c"
#include "../set_prot.c"
#include "../set_sysrev.c"

static void reset(void)
{
    mmu_pft_base = pft_store;
    mmu_ptt_base = ptt_store;
    mmu_csr = &hw_csr;
    mmu_hw_rev = &hw_rev;
    memset(pft_store, 0, sizeof pft_store);
    memset(ptt_store, 0, sizeof ptt_store);
    hw_csr = 0;
    mmu_pid_priv = 0x0500;
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
    mmu_pid_priv = 0x0303;
    MMU_$SET_CSR(0x0142);
    ASSERT_EQ(0x4203, mmu_pid_priv);
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
