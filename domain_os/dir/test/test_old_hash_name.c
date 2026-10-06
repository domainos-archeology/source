/*
 * dir/test/test_old_hash_name.c - dir_$old_hash_name (0x00E54B58)
 *
 * Pins: h = 2h + byte over the name in 16 bits, result h mod buckets; an
 * empty name hashes to 0.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
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

#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed); \
    return tests_failed ? 1 : 0; \
} while (0)

#include "../old_hash_name.c"

TEST(two_chars)
{
    ASSERT_EQ(5, dir_$old_hash_name((uint8_t *)"ab", 2, 7));    /* 0x124 % 7 */
}

TEST(empty_name)
{
    ASSERT_EQ(0, dir_$old_hash_name((uint8_t *)"x", 0, 43));
}

TEST(sixteen_bit_wrap)
{
    uint8_t n[20];
    memset(n, 0xFF, sizeof(n));
    ASSERT_EQ(7, dir_$old_hash_name(n, 20, 43));                /* 0xFF01 % 43 */
}

int main(void)
{
    printf("dir_$old_hash_name tests\n");
    RUN_TEST(two_chars);
    RUN_TEST(empty_name);
    RUN_TEST(sixteen_bit_wrap);
    TEST_SUMMARY();
}
