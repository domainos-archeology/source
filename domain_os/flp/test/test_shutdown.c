/*
 * flp/test/test_shutdown.c - FLP_$SHUTDOWN (0x00E3E228) and
 * FLP_$REVALIDATE (0x00E3DC54): the two per-unit flag routines.
 */

#include <stdio.h>
#include <string.h>

#include "flp/flp_internal.h"

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

#include "../flp_data.c"
#include "../shutdown.c"
#include "../revalidate.c"

/* 0x00E3E236-0x00E3E25A: the argument at (0xa,A6) is the unit; the count
 * covers all four units. */
TEST(shutdown_clears_the_unit_and_counts_the_rest)
{
    FLP_DATA.unit_active[0] = -1;
    FLP_DATA.unit_active[1] = -1;
    FLP_DATA.unit_active[2] = 0;
    FLP_DATA.unit_active[3] = -1;

    ASSERT_EQ(2, FLP_$SHUTDOWN(7, 1));
    ASSERT_EQ(0, FLP_DATA.unit_active[1]);
    ASSERT_EQ(-1, (int)FLP_DATA.unit_active[0]);
    ASSERT_EQ(-1, (int)FLP_DATA.unit_active[3]);
}

/* `tst.b` / `bpl`: only bytes with the sign bit set count. */
TEST(shutdown_counts_only_negative_bytes)
{
    FLP_DATA.unit_active[0] = 1;        /* positive: not active */
    FLP_DATA.unit_active[1] = 0x7F;
    FLP_DATA.unit_active[2] = (int8_t)0x80;
    FLP_DATA.unit_active[3] = 0;

    ASSERT_EQ(1, FLP_$SHUTDOWN(0, 3));
}

TEST(shutdown_of_the_last_unit_returns_zero)
{
    memset(FLP_DATA.unit_active, 0, sizeof(FLP_DATA.unit_active));
    FLP_DATA.unit_active[2] = -1;

    ASSERT_EQ(0, FLP_$SHUTDOWN(0, 2));
}

/* 0x00E3DC60-0x00E3DC6C: the volume's dev_unit (+0x1C) picks the flag. */
TEST(revalidate_clears_the_volume_unit_flag)
{
    disk_$volume_t vol;

    memset(&vol, 0, sizeof(vol));
    vol.dev_unit = 2;
    memset(FLP_DATA.disk_change, -1, sizeof(FLP_DATA.disk_change));

    FLP_$REVALIDATE(&vol);

    ASSERT_EQ(-1, (int)FLP_DATA.disk_change[0]);
    ASSERT_EQ(-1, (int)FLP_DATA.disk_change[1]);
    ASSERT_EQ(0, FLP_DATA.disk_change[2]);
    ASSERT_EQ(-1, (int)FLP_DATA.disk_change[3]);
}

int main(void)
{
    printf("FLP_$SHUTDOWN / FLP_$REVALIDATE tests\n");
    RUN_TEST(shutdown_clears_the_unit_and_counts_the_rest);
    RUN_TEST(shutdown_counts_only_negative_bytes);
    RUN_TEST(shutdown_of_the_last_unit_returns_zero);
    RUN_TEST(revalidate_clears_the_volume_unit_flag);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
