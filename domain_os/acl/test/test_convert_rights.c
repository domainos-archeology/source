/*
 * acl/test/test_convert_rights.c - unit tests for acl_$convert_rights
 * (0x00E44DBE).
 *
 * The real acl/convert_rights.c is #included at the bottom, so the bit
 * assignments under test are the real code.  The routine calls nothing, so the
 * only mocks needed are the two ACL type UIDs it compares against.
 */

#include <stdio.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-46s ", #name);                  \
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

#include "acl/acl_internal.h"

/* Raw bytes at 0x00E17444 / 0x00E1744C. */
uid_t ACL_$FILE_ACL = { 0x00000601u, 0 };
uid_t ACL_$DIR_ACL  = { 0x00000600u, 0 };

/* Neither of the two - exercises the "no bit assignment applies" path. */
static uid_t other_acl = { 0x00000602u, 0 };

/* ------------------------------------------------------------------ */
/* ACL_$FILE_ACL (0x00E44DD0-0x00E44E06)                                */
/* ------------------------------------------------------------------ */

TEST(file_acl_maps_old_bit_1_to_new_bit_2)
{
    /* `btst.l #0x1` + `moveq #0x4,D0`, 0x00E44DE2.  Old bit 3 is set so the
     * bit-6 clause below does not fire. */
    ASSERT_EQ(0x04, acl_$convert_rights(ACL_V4_RIGHT_1 | ACL_V4_RIGHT_3,
                                        &ACL_$FILE_ACL));
}

TEST(file_acl_maps_old_bit_2_to_new_bit_1)
{
    ASSERT_EQ(0x02, acl_$convert_rights(ACL_V4_RIGHT_2 | ACL_V4_RIGHT_3,
                                        &ACL_$FILE_ACL));
}

TEST(file_acl_maps_old_bit_0_to_new_bit_0)
{
    ASSERT_EQ(0x01, acl_$convert_rights(ACL_V4_RIGHT_0 | ACL_V4_RIGHT_3,
                                        &ACL_$FILE_ACL));
}

TEST(file_acl_sets_bit_6_when_old_bit_3_is_clear)
{
    /* 0x00E44DFE: `bne` skips the `ori.w #0x40` - it is the ABSENCE of the
     * old bit that grants the right. */
    ASSERT_EQ(0x40, acl_$convert_rights(0, &ACL_$FILE_ACL));
    ASSERT_EQ(0x00, acl_$convert_rights(ACL_V4_RIGHT_3, &ACL_$FILE_ACL));
}

TEST(file_acl_all_four_low_bits_give_0x07)
{
    /* The ACL_$FNDWRX default ACL: 0x3FFF-masked rights 0x000F with bit 25
     * forced on by acl_$expand_default_acl (0x00E45A44). */
    ASSERT_EQ(0x0F, acl_$convert_rights(0x0000000Fu | ACL_V4_RIGHT_25,
                                        &ACL_$FILE_ACL));
    ASSERT_EQ(0x07, acl_$convert_rights(0x0000000Fu, &ACL_$FILE_ACL));
}

TEST(file_acl_ignores_the_directory_only_bits)
{
    /* Old bits 4, 5 and 6 mean nothing to a file ACL; bit 3 keeps bit 6 off. */
    ASSERT_EQ(0x00, acl_$convert_rights(ACL_V4_RIGHT_3 | ACL_V4_RIGHT_4 |
                                        ACL_V4_RIGHT_5 | ACL_V4_RIGHT_6,
                                        &ACL_$FILE_ACL));
}

/* ------------------------------------------------------------------ */
/* ACL_$DIR_ACL (0x00E44E08-0x00E44E52)                                 */
/* ------------------------------------------------------------------ */

TEST(dir_acl_maps_old_bit_0_to_new_bit_2)
{
    /* Old bit 4 set so the bit-6 clause does not fire. */
    ASSERT_EQ(0x04, acl_$convert_rights(ACL_V4_RIGHT_0 | ACL_V4_RIGHT_4,
                                        &ACL_$DIR_ACL));
}

TEST(dir_acl_needs_all_four_of_bits_3_1_2_6_for_new_bit_1)
{
    uint32_t all = ACL_V4_RIGHT_3 | ACL_V4_RIGHT_1 | ACL_V4_RIGHT_2 |
                   ACL_V4_RIGHT_6 | ACL_V4_RIGHT_4;

    /* 0x00E44E24-0x00E44E3C: four chained `btst`/`beq`. */
    ASSERT_EQ(0x02, acl_$convert_rights(all, &ACL_$DIR_ACL));
    ASSERT_EQ(0x00, acl_$convert_rights(all & ~ACL_V4_RIGHT_3, &ACL_$DIR_ACL));
    ASSERT_EQ(0x00, acl_$convert_rights(all & ~ACL_V4_RIGHT_1, &ACL_$DIR_ACL));
    ASSERT_EQ(0x00, acl_$convert_rights(all & ~ACL_V4_RIGHT_2, &ACL_$DIR_ACL));
    ASSERT_EQ(0x00, acl_$convert_rights(all & ~ACL_V4_RIGHT_6, &ACL_$DIR_ACL));
}

TEST(dir_acl_maps_old_bit_5_to_new_bit_0)
{
    ASSERT_EQ(0x01, acl_$convert_rights(ACL_V4_RIGHT_5 | ACL_V4_RIGHT_4,
                                        &ACL_$DIR_ACL));
}

TEST(dir_acl_sets_bit_6_when_old_bit_4_is_clear)
{
    ASSERT_EQ(0x40, acl_$convert_rights(0, &ACL_$DIR_ACL));
    ASSERT_EQ(0x00, acl_$convert_rights(ACL_V4_RIGHT_4, &ACL_$DIR_ACL));
}

TEST(dir_acl_default_rights_convert_to_0x41)
{
    /* ACL_V4_RIGHTS_DEFAULT (0x1E0) is what the version-3/4 fixup gives the
     * appended world entry: old bits 5..8, so bit 5 -> new bit 0 and bit 4
     * stays clear -> new bit 6. */
    ASSERT_EQ(0x41, acl_$convert_rights(ACL_V4_RIGHTS_DEFAULT,
                                        &ACL_$DIR_ACL));
}

/* ------------------------------------------------------------------ */
/* Type-independent behaviour                                           */
/* ------------------------------------------------------------------ */

TEST(bit_25_always_contributes_new_bit_3)
{
    /* 0x00E44E54: outside both type blocks. */
    ASSERT_EQ(0x48, acl_$convert_rights(ACL_V4_RIGHT_25, &ACL_$FILE_ACL));
    ASSERT_EQ(0x48, acl_$convert_rights(ACL_V4_RIGHT_25, &ACL_$DIR_ACL));
    ASSERT_EQ(0x08, acl_$convert_rights(ACL_V4_RIGHT_25, &other_acl));
}

TEST(an_unknown_acl_type_contributes_nothing_else)
{
    ASSERT_EQ(0x00, acl_$convert_rights(0xFDFFFFFFu, &other_acl));
}

TEST(the_two_type_blocks_are_independent_ifs)
{
    /* ACL_$FILE_ACL and ACL_$DIR_ACL differ, so only one block can ever run;
     * the image still tests both (0x00E44E08 re-loads the UID).  Prove the
     * file result is not polluted by the directory assignment: old bit 5
     * means "new bit 0" only for directories. */
    ASSERT_EQ(0x00, acl_$convert_rights(ACL_V4_RIGHT_5 | ACL_V4_RIGHT_3,
                                        &ACL_$FILE_ACL));
}

int main(void)
{
    printf("acl_$convert_rights (0x00E44DBE)\n");

    RUN_TEST(file_acl_maps_old_bit_1_to_new_bit_2);
    RUN_TEST(file_acl_maps_old_bit_2_to_new_bit_1);
    RUN_TEST(file_acl_maps_old_bit_0_to_new_bit_0);
    RUN_TEST(file_acl_sets_bit_6_when_old_bit_3_is_clear);
    RUN_TEST(file_acl_all_four_low_bits_give_0x07);
    RUN_TEST(file_acl_ignores_the_directory_only_bits);

    RUN_TEST(dir_acl_maps_old_bit_0_to_new_bit_2);
    RUN_TEST(dir_acl_needs_all_four_of_bits_3_1_2_6_for_new_bit_1);
    RUN_TEST(dir_acl_maps_old_bit_5_to_new_bit_0);
    RUN_TEST(dir_acl_sets_bit_6_when_old_bit_4_is_clear);
    RUN_TEST(dir_acl_default_rights_convert_to_0x41);

    RUN_TEST(bit_25_always_contributes_new_bit_3);
    RUN_TEST(an_unknown_acl_type_contributes_nothing_else);
    RUN_TEST(the_two_type_blocks_are_independent_ifs);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../convert_rights.c"
