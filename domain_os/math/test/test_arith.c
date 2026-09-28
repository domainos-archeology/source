/*
 * math/test/test_arith.c - unit tests for the M$ARITH runtime routines
 * (math/mult.c, math/div.c, math/mod.c; 0x00E0ABD4 .. 0x00E0ADAF)
 *
 * Every expectation is the value the m68k instruction sequence produces,
 * including the truncations: only the low 32 bits of a product survive,
 * M$MIS$LLW keeps only the low word of its signed partial product, and the
 * shift-and-subtract loops lose a remainder bit shifted out of bit 31.
 */

#include <stdio.h>
#include <stdint.h>

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

#include "math/mult.c"
#include "math/div.c"
#include "math/mod.c"

static void test_miu_lll(void)
{
    ASSERT_EQ(6, M$MIU$LLL(2, 3));
    ASSERT_EQ(0x12345678u * 0x9ABCDEF0u, (uint32_t)M$MIU$LLL(0x12345678u, 0x9ABCDEF0u));
    ASSERT_EQ(0xFFFFFFFEu, (uint32_t)M$MIU$LLL(0xFFFFFFFFu, 2));    /* low 32 bits */
    ASSERT_EQ(0, (uint32_t)M$MIU$LLL(0x10000u, 0x10000u));         /* 2^32 truncates */
}

static void test_miu_llw(void)
{
    ASSERT_EQ(0x00030000u, (uint32_t)M$MIU$LLW(0x00010000u, 3));
    ASSERT_EQ(0xFFFE0001u, (uint32_t)M$MIU$LLW(0xFFFFu, 0xFFFF));
    ASSERT_EQ((uint32_t)(0x12345678u * 0x1234u), (uint32_t)M$MIU$LLW(0x12345678u, 0x1234));
}

static void test_mis_lll(void)
{
    ASSERT_EQ((uint32_t)-6, (uint32_t)M$MIS$LLL(-2, 3));
    ASSERT_EQ((uint32_t)-6, (uint32_t)M$MIS$LLL(2, -3));
    ASSERT_EQ(6, (uint32_t)M$MIS$LLL(-2, -3));
    ASSERT_EQ(0x80000000u, (uint32_t)M$MIS$LLL((long)(int32_t)0x80000000u, 1));  /* neg.l of INT_MIN */
    ASSERT_EQ(0, (uint32_t)M$MIS$LLL(0, -12345));
}

static void test_mis_llw(void)
{
    ASSERT_EQ((uint32_t)-6, (uint32_t)M$MIS$LLW(2, -3));
    ASSERT_EQ((uint32_t)-6, (uint32_t)M$MIS$LLW(-2, 3));
    ASSERT_EQ((uint32_t)(0x12345678 * 7), (uint32_t)M$MIS$LLW(0x12345678, 7));
    /* hi(mc) * mul = 0x7FFF * 3 = 0x17FFD, only the low word (0x7FFD) is kept */
    ASSERT_EQ((0x7FFDu << 16) + 0xFFFFu * 3u, (uint32_t)M$MIS$LLW(0x7FFFFFFF, 3));
}

static void test_diu_llw(void)
{
    ASSERT_EQ(0x12345678u / 0x1234u, (uint32_t)M$DIU$LLW(0x12345678u, 0x1234));
    ASSERT_EQ(0xFFFFFFFFu / 7u, (uint32_t)M$DIU$LLW(0xFFFFFFFFu, 7));
    ASSERT_EQ(0xFFFFFFFFu, (uint32_t)M$DIU$LLW(0xFFFFFFFFu, 1));
}

static void test_dis_llw(void)
{
    ASSERT_EQ((uint32_t)-4, (uint32_t)M$DIS$LLW(-13, 3));
    ASSERT_EQ((uint32_t)-4, (uint32_t)M$DIS$LLW(13, -3));
    ASSERT_EQ(4, (uint32_t)M$DIS$LLW(-13, -3));
    ASSERT_EQ(0x12345678 / 0x123, (uint32_t)M$DIS$LLW(0x12345678, 0x123));
}

static void test_diu_lll(void)
{
    ASSERT_EQ(0x12345678u / 0x1234u, (uint32_t)M$DIU$LLL(0x12345678u, 0x1234u));   /* divu path */
    ASSERT_EQ(0xFFFFFFFFu / 0x12345u, (uint32_t)M$DIU$LLL(0xFFFFFFFFu, 0x12345u));  /* loop path */
    ASSERT_EQ(1, (uint32_t)M$DIU$LLL(0xFFFFFFFFu, 0xFFFFFFFFu));
    ASSERT_EQ(0, (uint32_t)M$DIU$LLL(0x12345u, 0x12346u));
    ASSERT_EQ(0x80000000u / 0x10001u, (uint32_t)M$DIU$LLL(0x80000000u, 0x10001u));
}

static void test_dis_lll(void)
{
    ASSERT_EQ((uint32_t)-3, (uint32_t)M$DIS$LLL(-300000, 100000));
    ASSERT_EQ((uint32_t)-3, (uint32_t)M$DIS$LLL(300000, -100000));
    ASSERT_EQ(3, (uint32_t)M$DIS$LLL(-300000, -100000));
    ASSERT_EQ(3, (uint32_t)M$DIS$LLL(300000, 100000));
}

static void test_ois_lll(void)
{
    ASSERT_EQ(1, (uint32_t)M$OIS$LLL(7, 3));
    ASSERT_EQ((uint32_t)-1, (uint32_t)M$OIS$LLL(-7, 3));       /* sign follows the dividend */
    ASSERT_EQ(1, (uint32_t)M$OIS$LLL(7, -3));
    ASSERT_EQ((uint32_t)-1, (uint32_t)M$OIS$LLL(-7, -3));
    ASSERT_EQ(0x7FFFFFFFu % 0x12345u, (uint32_t)M$OIS$LLL(0x7FFFFFFF, 0x12345));
}

static void test_ois_wrappers(void)
{
    ASSERT_EQ(0xFFFF, (uint16_t)M$OIS$WLW(-7, 3));
    ASSERT_EQ(2, (uint16_t)M$OIS$WLW(100002, 100));
    ASSERT_EQ(0xFFFE, (uint16_t)M$OIS$WWL(-5, 3));
    ASSERT_EQ(5, (uint16_t)M$OIS$WWL(5, 100000));
}

static void test_oiu_wlw(void)
{
    ASSERT_EQ(0x12345678u % 0x1234u, (uint16_t)M$OIU$WLW(0x12345678, 0x1234));
    ASSERT_EQ(0xFFFFFFFFu % 7u, (uint16_t)M$OIU$WLW((long)(int32_t)0xFFFFFFFFu, 7));
    ASSERT_EQ(0xFFFFFFFFu % 0xFFFFu, (uint16_t)M$OIU$WLW((long)(int32_t)0xFFFFFFFFu, (short)0xFFFF));
}

int main(void)
{
    printf("M$ARITH tests:\n");
    RUN_TEST(miu_lll);
    RUN_TEST(miu_llw);
    RUN_TEST(mis_lll);
    RUN_TEST(mis_llw);
    RUN_TEST(diu_llw);
    RUN_TEST(dis_llw);
    RUN_TEST(diu_lll);
    RUN_TEST(dis_lll);
    RUN_TEST(ois_lll);
    RUN_TEST(ois_wrappers);
    RUN_TEST(oiu_wlw);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
