/*
 * xns/test/test_idp_checksum.c - Unit tests for the portable model of
 * XNS_IDP_$CHECKSUM (0x00E2B850) and XNS_IDP_$HOP_AND_SUM (0x00E2B872).
 *
 * The m68k build uses xns/sau2/idp_checksum.s (byte-identical to the image);
 * xns/idp_checksum.c is the host model of the same instruction sequence and
 * is what is exercised here.  Expected values were worked by hand from the
 * instruction trace in that file.
 */

#include <stdint.h>
#include <stdio.h>

static int tests_failed = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    unsigned long _a = (unsigned long)(a); \
    unsigned long _b = (unsigned long)(b); \
    if (_a != _b) { \
        printf("FAILED\n    %s = 0x%lx (%lu), %s = 0x%lx (%lu) at line %d\n", \
               #a, _a, _a, #b, _b, _b, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "../idp_checksum.c"

/* ==========================================================================
 * XNS_IDP_$CHECKSUM
 * ========================================================================== */

TEST(checksum_single_word_is_rotated_once)
{
    uint16_t w[1] = { 0x1234 };
    /* 0 + 0x1234 = 0x1234, rol 1 -> 0x2468 */
    ASSERT_EQ(XNS_IDP_$CHECKSUM(w, 1), 0x2468);
}

TEST(checksum_end_around_carry_then_rotate)
{
    uint16_t w[2] = { 0x8000, 0x8000 };
    /* 0x8000 -> rol -> 0x0001; 0x0001 + 0x8000 = 0x8001 (no carry) -> rol -> 0x0003 */
    ASSERT_EQ(XNS_IDP_$CHECKSUM(w, 2), 0x0003);

    uint16_t v[2] = { 0xFFFF, 0x0002 };
    /* 0xFFFF -> rol -> 0xFFFF; 0xFFFF + 2 = 0x10001 -> carry -> 0x0002 -> rol -> 0x0004 */
    ASSERT_EQ(XNS_IDP_$CHECKSUM(v, 2), 0x0004);
}

TEST(checksum_all_ones_result_becomes_zero)
{
    uint16_t w[1] = { 0xFFFF };
    /* 0xFFFF rol 1 = 0xFFFF -> reported as 0 */
    ASSERT_EQ(XNS_IDP_$CHECKSUM(w, 1), 0);

    uint16_t v[1] = { 0x7FFF };
    /* 0x7FFF rol 1 = 0xFFFE, not the reserved value */
    ASSERT_EQ(XNS_IDP_$CHECKSUM(v, 1), 0xFFFE);
}

TEST(checksum_zero_data_stays_zero)
{
    uint16_t w[8] = { 0 };
    ASSERT_EQ(XNS_IDP_$CHECKSUM(w, 8), 0);
}

/* ==========================================================================
 * XNS_IDP_$HOP_AND_SUM
 * ========================================================================== */

TEST(hop_and_sum_no_rotation_at_offset_3)
{
    /* ((3 - 3) >> 1) & 0xF = 0: contribution stays 0x100 */
    ASSERT_EQ((uint16_t)XNS_IDP_$HOP_AND_SUM(0x0010, 3), 0x0110);
    ASSERT_EQ((uint16_t)XNS_IDP_$HOP_AND_SUM(0x0010, 4), 0x0110);
}

TEST(hop_and_sum_rotates_by_word_index)
{
    /* offset 5: ((5 - 3) >> 1) = 1 -> 0x200 */
    ASSERT_EQ((uint16_t)XNS_IDP_$HOP_AND_SUM(0, 5), 0x0200);
    /* offset 0x21: (0x1E >> 1) & 0xF = 0xF -> 0x100 rol 15 = 0x0080 */
    ASSERT_EQ((uint16_t)XNS_IDP_$HOP_AND_SUM(0, 0x21), 0x0080);
    /* offset 0x23: (0x20 >> 1) & 0xF = 0 -> 0x100 */
    ASSERT_EQ((uint16_t)XNS_IDP_$HOP_AND_SUM(0, 0x23), 0x0100);
}

TEST(hop_and_sum_end_around_carry_and_all_ones)
{
    /* 0xFF80 + 0x100 = 0x10080 -> carry -> 0x0081 */
    ASSERT_EQ((uint16_t)XNS_IDP_$HOP_AND_SUM(0xFF80, 3), 0x0081);
    /* 0xFEFF + 0x100 = 0xFFFF -> reported as 0 */
    ASSERT_EQ((uint16_t)XNS_IDP_$HOP_AND_SUM(0xFEFF, 3), 0);
}

int main(void)
{
    printf("XNS_IDP_$CHECKSUM / XNS_IDP_$HOP_AND_SUM tests\n");
    RUN_TEST(checksum_single_word_is_rotated_once);
    RUN_TEST(checksum_end_around_carry_then_rotate);
    RUN_TEST(checksum_all_ones_result_becomes_zero);
    RUN_TEST(checksum_zero_data_stays_zero);
    RUN_TEST(hop_and_sum_no_rotation_at_offset_3);
    RUN_TEST(hop_and_sum_rotates_by_word_index);
    RUN_TEST(hop_and_sum_end_around_carry_and_all_ones);
    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
