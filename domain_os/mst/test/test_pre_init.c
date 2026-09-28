/*
 * mst/test/test_pre_init.c - unit tests for MST_$PRE_INIT (0x00E309F4)
 *
 * On a 68020 (high byte of M68020 negative) the fourteen layout words are
 * rewritten; on any machine MST_ASID_BASE[0..57] becomes
 * asid * ((MST_$SEG_TN + 63) div 64).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
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

#include "mst/mst_internal.h"

/* The module data mst_data.c and mmu_data.c would supply. */
uint16_t MST_$SEG_TN, MST_$GLOBAL_A_SIZE, MST_$SEG_GLOBAL_A, MST_$SEG_GLOBAL_A_END;
uint16_t MST_$PRIVATE_A_SIZE, MST_$SEG_PRIVATE_A_END, MST_$SEG_PRIVATE_B;
uint16_t MST_$SEG_PRIVATE_B_END, MST_$SEG_PRIVATE_B_OFFSET, MST_$SEG_GLOBAL_B;
uint16_t MST_$SEG_GLOBAL_B_OFFSET, MST_$SEG_HIGH, MST_$SEG_MEM_TOP, MST_$GLOBAL_B_SIZE;
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
uint16_t mmu_m68020;

#include "mst/pre_init.c"

static void reset_state(void)
{
    /* the mst_data.c (SAU2 / 68010) defaults */
    MST_$SEG_TN = 0x140;
    MST_$GLOBAL_A_SIZE = 0x60;
    MST_$SEG_GLOBAL_A = 0x138;
    MST_$SEG_GLOBAL_A_END = 0x197;
    MST_$PRIVATE_A_SIZE = 0x138;
    MST_$SEG_PRIVATE_A_END = 0x137;
    MST_$SEG_PRIVATE_B = 0x198;
    MST_$SEG_PRIVATE_B_END = 0x19F;
    MST_$SEG_PRIVATE_B_OFFSET = 0x60;
    MST_$SEG_GLOBAL_B = 0x1A0;
    MST_$SEG_GLOBAL_B_OFFSET = 0x140;
    MST_$SEG_HIGH = 0x1F8;
    MST_$SEG_MEM_TOP = 0x200;
    MST_$GLOBAL_B_SIZE = 0x60;
    memset(MST_ASID_BASE, 0xAA, sizeof(MST_ASID_BASE));
    mmu_m68020 = 0;
}

/* 68010 layout kept: 0x140 segments -> (0x140 + 63) / 64 = 5 words per ASID */
static void test_68010_keeps_layout_and_fills_bases(void)
{
    int i;

    MST_$PRE_INIT();

    ASSERT_EQ(0x140, MST_$SEG_TN);
    ASSERT_EQ(0x60, MST_$GLOBAL_A_SIZE);
    ASSERT_EQ(0x200, MST_$SEG_MEM_TOP);
    for (i = 0; i < MST_MAX_ASIDS; i++) {
        ASSERT_EQ(i * 5, MST_ASID_BASE[i]);
    }
    ASSERT_EQ(57 * 5, MST_ASID_BASE[57]);
}

/* 68020: the fourteen words at 0xE2444A..0xE24464, then 0x680 -> 26 per ASID */
static void test_68020_layout(void)
{
    mmu_m68020 = 0x8000;            /* `tst.b` of the high byte is negative */

    MST_$PRE_INIT();

    ASSERT_EQ(0x680, MST_$SEG_TN);
    ASSERT_EQ(0xe0, MST_$GLOBAL_A_SIZE);
    ASSERT_EQ(0x678, MST_$SEG_GLOBAL_A);
    ASSERT_EQ(0x757, MST_$SEG_GLOBAL_A_END);
    ASSERT_EQ(0x678, MST_$PRIVATE_A_SIZE);
    ASSERT_EQ(0x677, MST_$SEG_PRIVATE_A_END);
    ASSERT_EQ(0x758, MST_$SEG_PRIVATE_B);
    ASSERT_EQ(0x75f, MST_$SEG_PRIVATE_B_END);
    ASSERT_EQ(0xe0, MST_$SEG_PRIVATE_B_OFFSET);
    ASSERT_EQ(0x760, MST_$SEG_GLOBAL_B);
    ASSERT_EQ(0x680, MST_$SEG_GLOBAL_B_OFFSET);
    ASSERT_EQ(0x7e0, MST_$SEG_HIGH);
    ASSERT_EQ(0x800, MST_$SEG_MEM_TOP);
    ASSERT_EQ(0xa0, MST_$GLOBAL_B_SIZE);
    ASSERT_EQ(0, MST_ASID_BASE[0]);
    ASSERT_EQ(26, MST_ASID_BASE[1]);
    ASSERT_EQ(57 * 26, MST_ASID_BASE[57]);
}

/* A total that is not a multiple of 64 rounds up: 0x141 -> 6 words */
static void test_rounds_up(void)
{
    MST_$SEG_TN = 0x141;

    MST_$PRE_INIT();

    ASSERT_EQ(6, MST_ASID_BASE[1]);
    ASSERT_EQ(0x141, MST_$SEG_TN);
}

int main(void)
{
    printf("MST_$PRE_INIT tests:\n");
    RUN_TEST(68010_keeps_layout_and_fills_bases);
    RUN_TEST(68020_layout);
    RUN_TEST(rounds_up);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
