/*
 * uid/test/test_hash.c - unit tests for UID_$HASH (0x00E17360)
 *
 * Pins the fold (high ^ low, then hi16 ^ lo16), the `divu.w` + `swap`
 * packing (quotient in the HIGH word, remainder in the LOW word) and the
 * by-reference divisor.
 */

#include <stdio.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-52s ", #name);                  \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "uid/uid_internal.h"
#include "../hash.c"

TEST(remainder_in_low_word_quotient_in_high)
{
    uid_t u = { 0x00000000, 0x00001234 };   /* folded 16-bit value 0x1234 */
    uint16_t div = 0x25;                    /* audit's modulus 37 */
    uint32_t r = UID_$HASH(&u, &div);
    /* 0x1234 = 4660 = 37*125 + 35 */
    ASSERT_EQ(35, r & 0xFFFF);
    ASSERT_EQ(125, r >> 16);
}

TEST(fold_xors_all_four_halves)
{
    uid_t u = { 0xA1B2C3D4, 0x11223344 };
    uint16_t div = 1;
    uint32_t r = UID_$HASH(&u, &div);
    /* high^low = 0xB090F090; hi16 ^ lo16 = 0xB090 ^ 0xF090 = 0x4000 */
    ASSERT_EQ(0, r & 0xFFFF);
    ASSERT_EQ(0x4000, r >> 16);
}

TEST(divisor_is_read_through_the_pointer)
{
    uid_t u = { 0x0000FFFF, 0x00000000 };   /* folded value 0xFFFF */
    uint16_t div = 0xFFFF;
    uint32_t r = UID_$HASH(&u, &div);
    ASSERT_EQ(0, r & 0xFFFF);
    ASSERT_EQ(1, r >> 16);
    div = 0x10;
    r = UID_$HASH(&u, &div);
    ASSERT_EQ(0xF, r & 0xFFFF);
    ASSERT_EQ(0xFFF, r >> 16);
}

/* the modulus 17 used by FILE_$UID_LOCK_* */
TEST(file_lock_modulus_17)
{
    uid_t u = { 0x12345678, 0x9ABCDEF0 };
    uint16_t div = 17;
    uint32_t r = UID_$HASH(&u, &div);
    /* high^low = 0x88888888; hi16^lo16 = 0; 0 % 17 = 0 */
    ASSERT_EQ(0, r & 0xFFFF);
    ASSERT_EQ(0, r >> 16);
}

int main(void)
{
    printf("UID_$HASH tests\n");
    RUN_TEST(remainder_in_low_word_quotient_in_high);
    RUN_TEST(fold_xors_all_four_halves);
    RUN_TEST(divisor_is_read_through_the_pointer);
    RUN_TEST(file_lock_modulus_17);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
