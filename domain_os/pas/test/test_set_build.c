/*
 * pas/test/test_set_build.c - unit tests for PAS_$SET_BUILD (0x00E11FA8)
 *
 * Member N of a set is bit N & 7 of byte ((total | 0xF) - N) >> 3.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

#include "pas/pas_internal.h"
#include "pas/set_build.c"

static uint16_t src[8];
static uint16_t dst[8];

static void reset(void)
{
    memset(src, 0, sizeof(src));
    memset(dst, 0xAA, sizeof(dst));
}

/* a 32-member set: (32 >> 4) + 1 = 3 words copied, members 3..5 set */
static void test_copies_and_sets_range(void)
{
    uint8_t *b = (uint8_t *)dst;

    reset();
    src[0] = 0x1111; src[1] = 0x2222; src[2] = 0x3333;
    PAS_$SET_BUILD(dst, src, 3, 5, 32);

    ASSERT_EQ(0xAAAA, dst[3]);                        /* not copied */
    /* total|0xF = 0x2F; member 3 -> byte (0x2F-3)>>3 = 5, bit 3; 4 -> byte 5
     * bit 4; 5 -> byte 5 bit 5 - or-ed into the copied template byte */
    ASSERT_EQ(0x33 | 0x38, b[5]);
    ASSERT_EQ(0x33, b[4]);
    ASSERT_EQ(0x11, b[0]);                            /* untouched template bytes */
}

/* lo below zero starts at 0; hi above total stops at total (inclusive) */
static void test_clamps(void)
{
    uint8_t *b = (uint8_t *)dst;

    reset();
    PAS_$SET_BUILD(dst, src, -5, 100, 16);
    /* total|0xF = 0x1F; members 0..16: member 0 -> byte 3 bit 0 ... 7 -> byte
     * 3 bit 7; 8..15 -> byte 2; 16 -> byte 1 bit 0 */
    ASSERT_EQ(0xFF, b[3]);
    ASSERT_EQ(0xFF, b[2]);
    ASSERT_EQ(0x01, b[1]);
    ASSERT_EQ(0x00, b[0]);
}

/* lo > hi sets nothing but still copies the template */
static void test_empty_range(void)
{
    reset();
    src[0] = 0x5555;
    PAS_$SET_BUILD(dst, src, 9, 4, 15);
    ASSERT_EQ(0x5555, dst[0]);
    ASSERT_EQ(0xAAAA, dst[1]);
}

int main(void)
{
    printf("PAS_$SET_BUILD tests:\n");
    RUN_TEST(copies_and_sets_range);
    RUN_TEST(clamps);
    RUN_TEST(empty_range);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
