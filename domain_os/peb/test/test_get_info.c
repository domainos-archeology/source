/*
 * peb/test/test_get_info.c - PEB_$GET_INFO (0x00E709E8)
 *
 * The five flags PEB_$GET_INFO reports are Domain booleans tested with
 * tst.b + bpl on the signed byte (0x00E709F2..0x00E70A28), so a 0xFF flag
 * sets its bit and a 0 flag leaves it clear (source-uw36: with uint8_t
 * fields every arm was dead).
 */

#include <stdio.h>
#include <string.h>

#include "peb/peb_internal.h"

MODULE_DATA_DEFINE(peb_globals_t, PEB_$INFO, 0x00E24C78);

#include "../get_info.c"

static int tests_passed = 0;
static int tests_failed = 0;
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { printf("  Running %-44s ", #name); test_##name(); \
    tests_passed++; printf("PASSED\n"); } while (0)
#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
        _e, _a, __LINE__); tests_failed++; return; } } while (0)

TEST(all_clear_reports_nothing)
{
    uint16_t flags = 0xFFFF; uint8_t info = 0xAA;
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$INFO.info_byte = 0x5A;
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ(0, flags);            /* clr.w (A0): both bytes */
    ASSERT_EQ(0x5A, info);          /* move.b (0xE24C96),(A1) */
}

TEST(each_true_flag_sets_its_bit_in_the_high_byte)
{
    uint16_t flags; uint8_t info;
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);

    PEB_$INFO.wcs_loaded = -1;
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ(PEB_INFO_WCS_LOADED << 8, flags);         /* bset.b #7 */

    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$INFO.m68881_save_flag = -1;
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ(PEB_INFO_M68881_MODE << 8, flags);        /* bset.b #6 */

    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$INFO.savep_flag = -1;
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ(PEB_INFO_SAVEP_FLAG << 8, flags);         /* bset.b #5 */

    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$INFO.flag_1d = -1;
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ(PEB_INFO_UNKNOWN_08 << 8, flags);         /* bset.b #3 */

    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$INFO.flag_21 = -1;
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ(PEB_INFO_UNKNOWN_10 << 8, flags);         /* bset.b #4 */
}

TEST(installed_and_mmu_installed_are_not_reported)
{
    uint16_t flags; uint8_t info;
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$INFO.installed = -1;
    PEB_$INFO.mmu_installed = -1;
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ(0, flags);
}

TEST(positive_nonzero_byte_is_false)
{
    /* bpl: only bit 7 of the byte counts. */
    uint16_t flags; uint8_t info;
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$INFO.wcs_loaded = 0x7F;
    PEB_$INFO.savep_flag = 1;
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ(0, flags);
}

TEST(all_five_true)
{
    uint16_t flags; uint8_t info;
    memset(&PEB_$INFO, -1, sizeof PEB_$INFO);
    PEB_$GET_INFO(&flags, &info);
    ASSERT_EQ((PEB_INFO_WCS_LOADED | PEB_INFO_M68881_MODE | PEB_INFO_SAVEP_FLAG |
               PEB_INFO_UNKNOWN_08 | PEB_INFO_UNKNOWN_10) << 8, flags);
    ASSERT_EQ(0xFF, info);
}

int main(void)
{
    printf("PEB_$GET_INFO tests\n");
    RUN_TEST(all_clear_reports_nothing);
    RUN_TEST(each_true_flag_sets_its_bit_in_the_high_byte);
    RUN_TEST(installed_and_mmu_installed_are_not_reported);
    RUN_TEST(positive_nonzero_byte_is_false);
    RUN_TEST(all_five_true);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
