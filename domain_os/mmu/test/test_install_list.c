/*
 * mmu/test/test_install_list.c - Unit tests for MMU_$INSTALL_LIST
 * (0x00E23FDE) and MMU_$INSTALL_PRIVATE (0x00E23F82)
 *
 * mmu_$installi is mocked; the CSR and the module cells are the host
 * globals from mmu/mmu.h's !ARCH_M68K branch.
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

static volatile uint16_t hw_csr;
static uint32_t pft_store[0x1000];

static int calls;
static uint16_t got_ppn[8];
static uint32_t got_va[8], got_packed[8];
static uint16_t csr_during_call;

void mmu_$installi(uint16_t ppn, uint32_t va, uint32_t packed)
{
    if (calls < 8) { got_ppn[calls] = ppn; got_va[calls] = va; got_packed[calls] = packed; }
    calls++;
    csr_during_call = hw_csr;
}

#include "../install_list.c"
#include "../install_private.c"

static void reset(void)
{
    mmu_csr = &hw_csr;
    mmu_pft_base = pft_store;
    memset(pft_store, 0, sizeof pft_store);
    hw_csr = 0;
    mmu_pid_priv = 0x0500;
    mmu_m68020 = 0x0100;
    calls = 0;
}

/* pack(va, asid, prot) exactly as the image does on a 68020 */
static uint32_t pack(uint32_t va, uint8_t asid, uint8_t prot)
{
    uint32_t p = va << 8;
    p = (p & 0xFFFFFF00u) | prot;
    p = (p >> 5) | (p << 27);
    p = (p & 0xFFFFFF00u) | asid;
    p = (p >> 7) | (p << 25);
    return p & 0xFFFFFFF0u;
}

TEST(list_installs_each_page_in_order)
{
    uint32_t ppns[3] = { 0x11, 0x22, 0x33 };
    reset();
    MMU_$INSTALL_LIST(3, ppns, 0x20000, (5u << 16) | 6u);
    ASSERT_EQ(3, calls);
    ASSERT_EQ(0x11, got_ppn[0]);
    ASSERT_EQ(0x33, got_ppn[2]);
    ASSERT_EQ(0x20000, got_va[0]);
    ASSERT_EQ(0x20800, got_va[2]);
    ASSERT_EQ(pack(0x20000, 5, 6), got_packed[0]);
    ASSERT_EQ((pack(0x20000, 5, 6) & 0xFFFF0000u) | ((pack(0x20000, 5, 6) + 0x20) & 0xFFFF),
              got_packed[2]);
    ASSERT_EQ(0x0502, csr_during_call);
    ASSERT_EQ(0x0500, hw_csr);
}

/* `add.w #0x10,D5w`: the carry stays in the low word */
TEST(list_packed_increment_is_a_word_add)
{
    uint32_t ppns[2] = { 1, 2 };
    reset();
    /*
     * asid 0xFF and an all-ones va pack (on a 68020) to 0xFE0FFFF0, whose
     * low word is 0xFFF0: the asid byte sits under the low word after the
     * two rotates, so a zero asid can never produce it.
     */
    MMU_$INSTALL_LIST(2, ppns, 0xFFFFFFFFu, 0xFFu << 16);
    ASSERT_EQ(0xFFF0, got_packed[0] & 0xFFFF);
    ASSERT_EQ(got_packed[0] & 0xFFFF0000u, got_packed[1] & 0xFFFF0000u);
    ASSERT_EQ(0x0000, got_packed[1] & 0xFFFF);
}

TEST(list_68010_shifts_low_word)
{
    uint32_t ppns[1] = { 9 };
    reset();
    mmu_m68020 = 0;
    MMU_$INSTALL_LIST(1, ppns, 0x1000, (2u << 16) | 3u);
    {
        uint32_t p = 0x1000u << 8;
        p = (p & 0xFFFFFF00u) | 3;
        p = (p >> 5) | (p << 27);
        p = (p & 0xFFFFFF00u) | 2;
        p = (p >> 7) | (p << 25);
        p = (p & 0xFFFF0000u) | ((p & 0xFFFF) >> 2);
        p &= 0xFFFFFFF0u;
        ASSERT_EQ(p, got_packed[0]);
    }
}

TEST(private_clears_global_bit_and_restores_csr)
{
    reset();
    pft_store[0x22] = 0x12345678u | 0x1000u;
    MMU_$INSTALL_PRIVATE(0x22, 0x3000, (7u << 16) | 1u);
    ASSERT_EQ(1, calls);
    ASSERT_EQ(0x22, got_ppn[0]);
    ASSERT_EQ(pack(0x3000, 7, 1), got_packed[0]);
    ASSERT_EQ(0x12344678u, pft_store[0x22]);
    ASSERT_EQ(0x0502, csr_during_call);
    ASSERT_EQ(0x0500, hw_csr);
}

int main(void)
{
    printf("test_install_list:\n");
    RUN_TEST(list_installs_each_page_in_order);
    RUN_TEST(list_packed_increment_is_a_word_add);
    RUN_TEST(list_68010_shifts_low_word);
    RUN_TEST(private_clears_global_bit_and_restores_csr);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
