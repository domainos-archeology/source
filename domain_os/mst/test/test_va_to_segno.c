/*
 * mst/test/test_va_to_segno.c - unit tests for MST_$VA_TO_SEGNO (0x00E0DCC8)
 *
 * Uses the SAU2 default layout from mst_data.c: private A 0..0x137,
 * global A 0x138..0x197, private B 0x198..0x19F, global B 0x1A0..0x1FF,
 * memory top 0x200.
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

uint16_t MST_$SEG_MEM_TOP, MST_$PRIVATE_A_SIZE, MST_$SEG_PRIVATE_B;
uint16_t MST_$SEG_GLOBAL_A, MST_$GLOBAL_A_SIZE, MST_$SEG_GLOBAL_B;

#include "mst/va_to_segno.c"

static void reset_state(void)
{
    MST_$SEG_MEM_TOP = 0x200;
    MST_$PRIVATE_A_SIZE = 0x138;
    MST_$SEG_PRIVATE_B = 0x198;
    MST_$SEG_GLOBAL_A = 0x138;
    MST_$GLOBAL_A_SIZE = 0x60;
    MST_$SEG_GLOBAL_B = 0x1A0;
}

static void test_private_a(void)
{
    uint16_t segno = 0xDEAD;

    ASSERT_EQ(0x77, MST_$VA_TO_SEGNO(0x00000000, &segno, 0x77));
    ASSERT_EQ(0, segno);
    ASSERT_EQ(0x77, MST_$VA_TO_SEGNO(0x137u << 15 | 0x7FFF, &segno, 0x77));
    ASSERT_EQ(0x137, segno);
}

static void test_private_b(void)
{
    uint16_t segno = 0xDEAD;

    ASSERT_EQ(0x77, MST_$VA_TO_SEGNO(0x198u << 15, &segno, 0x77));
    ASSERT_EQ(0x138, segno);
    ASSERT_EQ(0x77, MST_$VA_TO_SEGNO(0x19Fu << 15, &segno, 0x77));
    ASSERT_EQ(0x13F, segno);
}

static void test_global_a(void)
{
    uint16_t segno = 0xDEAD;

    ASSERT_EQ(0, MST_$VA_TO_SEGNO(0x138u << 15, &segno, 0x77));
    ASSERT_EQ(0, segno);
    ASSERT_EQ(0, MST_$VA_TO_SEGNO(0x197u << 15, &segno, 0x77));
    ASSERT_EQ(0x5F, segno);
}

static void test_global_b(void)
{
    uint16_t segno = 0xDEAD;

    ASSERT_EQ(0, MST_$VA_TO_SEGNO(0x1A0u << 15, &segno, 0x77));
    ASSERT_EQ(0x60, segno);
    ASSERT_EQ(0, MST_$VA_TO_SEGNO(0x1FFu << 15, &segno, 0x77));
    ASSERT_EQ(0xBF, segno);
}

/* A gap between global A and global B (0x00E0DD34): 0x3a, segno untouched */
static void test_gap_returns_0x3a(void)
{
    uint16_t segno = 0xDEAD;

    MST_$GLOBAL_A_SIZE = 0x40;           /* global A now ends at 0x177 */
    ASSERT_EQ(0x3a, MST_$VA_TO_SEGNO(0x180u << 15, &segno, 0x77));
    ASSERT_EQ(0xDEAD, segno);
}

/* At or above SEG_MEM_TOP the compare is on the 32-bit segment number
 * (`cmp.l D3,D1` at 0x00E0DCEE): 0 comes back and segno is untouched, even
 * when the low word of the segment number would land in private A. */
static void test_memory_top_uses_32_bit_segno(void)
{
    uint16_t segno = 0xDEAD;

    ASSERT_EQ(0, MST_$VA_TO_SEGNO(0x200u << 15, &segno, 0x77));
    ASSERT_EQ(0xDEAD, segno);
    ASSERT_EQ(0, MST_$VA_TO_SEGNO(0x80000000u, &segno, 0x77));   /* segno 0x10000 */
    ASSERT_EQ(0xDEAD, segno);
    ASSERT_EQ(0, MST_$VA_TO_SEGNO(0xFFFFFFFFu, &segno, 0x77));
    ASSERT_EQ(0xDEAD, segno);
}

int main(void)
{
    printf("MST_$VA_TO_SEGNO tests:\n");
    RUN_TEST(private_a);
    RUN_TEST(private_b);
    RUN_TEST(global_a);
    RUN_TEST(global_b);
    RUN_TEST(gap_returns_0x3a);
    RUN_TEST(memory_top_uses_32_bit_segno);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
