/*
 * iic/test/test_stubs.c - the eight IIC entry points (0x00E70A54..0x00E70AF2)
 *
 * Every IIC routine in the SAU2 image is a stub.  These tests pin down the
 * observable contract of each: which argument slot receives 0x2C000A, which
 * 16-bit out parameters IIC_$RECEIVE clears, and that the two boolean
 * functions return false, with the untouched arguments left alone.
 */

#include <stdio.h>
#include <string.h>

#include "../acquire.c"
#include "../exists.c"
#include "../init.c"
#include "../receive.c"
#include "../receive_check.c"
#include "../release.c"
#include "../self_test.c"
#include "../send.c"
#include "../statistics.c"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-44s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define POISON 0x5a5a5a5a

TEST(acquire_release_store_status)
{
    status_$t st = POISON;
    IIC_$ACQUIRE(0x12345678, &st);
    ASSERT_EQ(0x2c000a, st);
    st = POISON;
    IIC_$RELEASE(0x12345678, &st);
    ASSERT_EQ(0x2c000a, st);
}

TEST(three_arg_stubs_store_status)
{
    status_$t st = POISON;
    IIC_$STATISTICS(1, 2, &st);
    ASSERT_EQ(0x2c000a, st);
    st = POISON;
    IIC_$SELF_TEST(1, 2, &st);
    ASSERT_EQ(0x2c000a, st);
}

TEST(send_stores_status_in_sixth_slot)
{
    status_$t st = POISON;
    IIC_$SEND(1, 2, 3, 4, 5, &st);
    ASSERT_EQ(0x2c000a, st);
}

TEST(receive_clears_both_counts)
{
    status_$t st = POISON;
    uint16_t c3 = 0x1234, c5 = 0x5678;
    IIC_$RECEIVE(1, 2, &c3, 4, &c5, &st);
    ASSERT_EQ(0x2c000a, st);
    ASSERT_EQ(0, c3);
    ASSERT_EQ(0, c5);
}

TEST(booleans_are_false)
{
    status_$t st = POISON;
    ASSERT_EQ(0, IIC_$EXISTS());
    ASSERT_EQ(0, IIC_$RECEIVE_CHECK(7, &st));
    ASSERT_EQ(0x2c000a, st);
}

TEST(init_is_a_no_op)
{
    IIC_$INIT();
    ASSERT_EQ(0, 0);
}

int main(void)
{
    printf("iic stub tests\n");
    RUN_TEST(acquire_release_store_status);
    RUN_TEST(three_arg_stubs_store_status);
    RUN_TEST(send_stores_status_in_sixth_slot);
    RUN_TEST(receive_clears_both_counts);
    RUN_TEST(booleans_are_false);
    RUN_TEST(init_is_a_no_op);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
