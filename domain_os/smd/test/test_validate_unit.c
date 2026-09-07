/*
 * smd/test/test_validate_unit.c - Unit tests for smd_$validate_unit
 * (0x00E6D700).
 *
 * The real smd/validate_unit.c is #included below and the real function is
 * called.
 *
 * Facts under test:
 *   - only unit 1 is accepted: 0x00E6D70A "cmpi.w #0x1,D0w" / 0x00E6D70E
 *     "bne.b 0x00e6d72c" -> 0x00E6D72C "clr.b D0b"
 *   - for unit 1 the answer is the Domain boolean
 *     (SMD_DISPLAY_INFO[0].display_type != 0): 0x00E6D722
 *     "tst.w (-0x60,A0,D1*0x1)" / 0x00E6D726 "sne D1b"
 *   - the info table is 1-based on the unit number, and it has exactly one
 *     entry - 0x00E27376..0x00E273D5, with SMD_TIME_$COM at 0x00E273D6
 *     (bead source-9j2l)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "smd/smd_internal.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-44s", #name);                                             \
        current_failed = 0;                                                   \
        test_##name();                                                        \
        if (current_failed) {                                                 \
            tests_failed++;                                                   \
        } else {                                                              \
            tests_passed++;                                                   \
            printf("PASSED\n");                                               \
        }                                                                     \
    } while (0)

#define CHECK_EQ(expected, actual)                                            \
    do {                                                                      \
        long _e = (long)(expected);                                           \
        long _a = (long)(actual);                                             \
        if (_e != _a) {                                                       \
            if (!current_failed) printf("FAILED\n");                          \
            current_failed = 1;                                               \
            printf("      %s:%d: %s: expected 0x%lx, got 0x%lx\n", __FILE__,  \
                   __LINE__, #actual, (unsigned long)_e, (unsigned long)_a);  \
        }                                                                     \
    } while (0)

/*
 * The table length the image actually has, and the size of one entry that
 * makes the "-0x60" bias in the addressing work out.
 */
_Static_assert(SMD_DISPLAY_INFO_COUNT == 1,
               "SMD_$DISPLAY_COM 0x00E27376..0x00E273D5 is one 0x60-byte entry");
/* The image stride is 0x60 (00e6d71a "lsl.l #5" then two adds = *96).  Assert
 * against SMD_DISPLAY_INFO_SIZE rather than sizeof(), because
 * smd_display_hw_t holds pointers and so is wider on a 64-bit host. */
_Static_assert(0x00E27376 + SMD_DISPLAY_INFO_SIZE * SMD_DISPLAY_INFO_COUNT
                   == 0x00E273D6,
               "the table must end exactly where SMD_TIME_$COM starts");

/* ------------------------------------------------------------------ */
/* Mocked globals                                                      */
/* ------------------------------------------------------------------ */

smd_display_info_t SMD_DISPLAY_INFO[SMD_DISPLAY_INFO_COUNT];

/* ------------------------------------------------------------------ */
/* Function under test                                                 */
/* ------------------------------------------------------------------ */

#include "../validate_unit.c"

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

TEST(unit1_configured)
{
    /* 1-based: unit 1 is SMD_DISPLAY_INFO[0] */
    SMD_DISPLAY_INFO[0].display_type = SMD_DISP_TYPE_MONO_PORTRAIT;
    CHECK_EQ((int8_t)0xFF, smd_$validate_unit(1));
}

TEST(unit1_unconfigured)
{
    SMD_DISPLAY_INFO[0].display_type = 0;
    CHECK_EQ(0, smd_$validate_unit(1));
}

TEST(unit0_invalid)
{
    SMD_DISPLAY_INFO[0].display_type = SMD_DISP_TYPE_MONO_PORTRAIT;
    CHECK_EQ(0, smd_$validate_unit(0));
}

TEST(unit2_invalid)
{
    /* There is no entry 2 in the image, and 0x00E6D70E never lets the code
     * reach the table for unit 2. */
    SMD_DISPLAY_INFO[0].display_type = SMD_DISP_TYPE_MONO_PORTRAIT;
    CHECK_EQ(0, smd_$validate_unit(2));
}

TEST(unit3_invalid)
{
    SMD_DISPLAY_INFO[0].display_type = SMD_DISP_TYPE_MONO_PORTRAIT;
    CHECK_EQ(0, smd_$validate_unit(3));
}

TEST(unit_max_invalid)
{
    SMD_DISPLAY_INFO[0].display_type = SMD_DISP_TYPE_MONO_PORTRAIT;
    CHECK_EQ(0, smd_$validate_unit(0xFFFF));
}

TEST(unit1_various_types)
{
    /* `sne` makes every non-zero display type valid. */
    static const uint16_t types[] = { 1, 2, 3, 4, 5, 6, 8, 9, 10, 11 };
    for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        SMD_DISPLAY_INFO[0].display_type = types[i];
        CHECK_EQ((int8_t)0xFF, smd_$validate_unit(1));
    }
}

TEST(unit_info_is_one_based)
{
    /* smd_$unit_info(1) is the one entry the table has. */
    CHECK_EQ((uintptr_t)&SMD_DISPLAY_INFO[0], (uintptr_t)smd_$unit_info(1));
}

int main(void)
{
    printf("test_validate_unit:\n");

    RUN_TEST(unit1_configured);
    RUN_TEST(unit1_unconfigured);
    RUN_TEST(unit0_invalid);
    RUN_TEST(unit2_invalid);
    RUN_TEST(unit3_invalid);
    RUN_TEST(unit_max_invalid);
    RUN_TEST(unit1_various_types);
    RUN_TEST(unit_info_is_one_based);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
