/*
 * os/test/test_data_copy_zero.c - unit tests for OS_$DATA_COPY (0x00E11F04)
 * and OS_$DATA_ZERO (0x00E11F42)
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

#include "os/os_internal.h"
#include "os/data_copy.c"
#include "os/data_zero.c"

static uint8_t src[64] __attribute__((aligned(4)));
static uint8_t dst[64] __attribute__((aligned(4)));

static void fill(void)
{
    int i;
    for (i = 0; i < 64; i++) {
        src[i] = (uint8_t)(0x40 + i);
        dst[i] = 0xEE;
    }
}

/* aligned: 4-byte moves then the byte tail */
static void test_copy_aligned(void)
{
    fill();
    OS_$DATA_COPY(src, dst, 11);
    ASSERT_EQ(0, memcmp(src, dst, 11));
    ASSERT_EQ(0xEE, dst[11]);
}

/* an odd address on either side goes byte by byte */
static void test_copy_unaligned(void)
{
    fill();
    OS_$DATA_COPY(src + 1, dst, 9);
    ASSERT_EQ(0, memcmp(src + 1, dst, 9));
    ASSERT_EQ(0xEE, dst[9]);
    fill();
    OS_$DATA_COPY(src, dst + 1, 9);
    ASSERT_EQ(0, memcmp(src, dst + 1, 9));
    ASSERT_EQ(0xEE, dst[0]);
    ASSERT_EQ(0xEE, dst[10]);
}

/* only the low word of the length is used; a low word of 0 copies nothing */
static void test_copy_length_low_word(void)
{
    fill();
    OS_$DATA_COPY(src, dst, 0x10000);
    ASSERT_EQ(0xEE, dst[0]);
    fill();
    OS_$DATA_COPY(src, dst, 0);
    ASSERT_EQ(0xEE, dst[0]);
}

static void test_zero_aligned(void)
{
    fill();
    OS_$DATA_ZERO(dst + 4, 11);          /* 2 longwords, a word, a byte */
    ASSERT_EQ(0xEE, dst[3]);
    ASSERT_EQ(0, dst[4]);
    ASSERT_EQ(0, dst[14]);
    ASSERT_EQ(0xEE, dst[15]);
}

/* an odd start is aligned with a single byte first */
static void test_zero_unaligned(void)
{
    fill();
    OS_$DATA_ZERO(dst + 5, 6);
    ASSERT_EQ(0xEE, dst[4]);
    ASSERT_EQ(0, dst[5]);
    ASSERT_EQ(0, dst[10]);
    ASSERT_EQ(0xEE, dst[11]);
}

static void test_zero_small(void)
{
    fill();
    OS_$DATA_ZERO(dst, 0);
    ASSERT_EQ(0xEE, dst[0]);
    OS_$DATA_ZERO(dst, 1);
    ASSERT_EQ(0, dst[0]);
    ASSERT_EQ(0xEE, dst[1]);
    fill();
    OS_$DATA_ZERO(dst, 3);               /* fewer than 4: a word and a byte */
    ASSERT_EQ(0, dst[2]);
    ASSERT_EQ(0xEE, dst[3]);
}

int main(void)
{
    printf("OS_$DATA_COPY / OS_$DATA_ZERO tests:\n");
    RUN_TEST(copy_aligned);
    RUN_TEST(copy_unaligned);
    RUN_TEST(copy_length_low_word);
    RUN_TEST(zero_aligned);
    RUN_TEST(zero_unaligned);
    RUN_TEST(zero_small);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
