/*
 * acl/test/test_in_subsys.c - ACL_$IN_SUBSYS (0x00E47098)
 *
 * Bead source-lpk8: the routine's answer is a Domain BOOLEAN BYTE.
 *
 *   00e470a4  move.w (0x00e20608).l,D0w    PROC1_$CURRENT
 *   00e470b0  add.w D0w,D0w                *2, the table holds words
 *   00e470b6  tst.w (-0x3d5a,A1)           ACL_$SUBSYS_LEVEL[current]
 *   00e470ba  sgt D0b                      SIGNED greater-than, LOW BYTE only
 *
 * `sgt D0b` writes one byte, so D0's high half still holds PROC1_$CURRENT * 2
 * from 0x00E470B0; both callers read the result with `tst.b D0b / bpl`
 * (0x00E615AC, 0x00E61B28).  The C return type is therefore `boolean`.
 */

#include <stdio.h>
#include <string.h>

#include "acl/acl_internal.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-40s ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    long long _e = (long long)(expected);                                \
    long long _a = (long long)(actual);                                  \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected %lld, got %lld at line %d\n",       \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

/* ==========================================================================
 * Globals the code under test reaches
 * ========================================================================== */

uint16_t PROC1_$CURRENT;
int16_t ACL_$SUBSYS_LEVEL[PROC1_MAX_PROCESSES];

#include "../in_subsys.c"

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* The result is a byte-wide Domain boolean, so it must be signed-negative. */
TEST(true_is_a_negative_byte)
{
    memset(ACL_$SUBSYS_LEVEL, 0, sizeof(ACL_$SUBSYS_LEVEL));
    PROC1_$CURRENT = 4;
    ACL_$SUBSYS_LEVEL[4] = 1;

    ASSERT_EQ(sizeof(int8_t), sizeof(ACL_$IN_SUBSYS()));
    ASSERT_EQ((int8_t)0xFF, ACL_$IN_SUBSYS());
    /* the way both callers read it */
    ASSERT_EQ(1, ACL_$IN_SUBSYS() < 0);
}

TEST(zero_level_is_false)
{
    memset(ACL_$SUBSYS_LEVEL, 0, sizeof(ACL_$SUBSYS_LEVEL));
    PROC1_$CURRENT = 4;
    ACL_$SUBSYS_LEVEL[4] = 0;

    ASSERT_EQ(0, ACL_$IN_SUBSYS());
    ASSERT_EQ(0, ACL_$IN_SUBSYS() < 0);
}

/* "sgt" is a SIGNED test, so a negative level is false, not true. */
TEST(negative_level_is_false)
{
    memset(ACL_$SUBSYS_LEVEL, 0, sizeof(ACL_$SUBSYS_LEVEL));
    PROC1_$CURRENT = 4;
    ACL_$SUBSYS_LEVEL[4] = -1;

    ASSERT_EQ(0, ACL_$IN_SUBSYS());
}

/* The table is indexed by PROC1_$CURRENT, one word per process. */
TEST(indexed_by_the_current_process)
{
    memset(ACL_$SUBSYS_LEVEL, 0, sizeof(ACL_$SUBSYS_LEVEL));
    ACL_$SUBSYS_LEVEL[7] = 3;

    PROC1_$CURRENT = 6;
    ASSERT_EQ(0, ACL_$IN_SUBSYS());
    PROC1_$CURRENT = 7;
    ASSERT_EQ((int8_t)0xFF, ACL_$IN_SUBSYS());
    PROC1_$CURRENT = 8;
    ASSERT_EQ(0, ACL_$IN_SUBSYS());
}

int main(void)
{
    printf("ACL_$IN_SUBSYS tests\n");
    RUN_TEST(true_is_a_negative_byte);
    RUN_TEST(zero_level_is_false);
    RUN_TEST(negative_level_is_false);
    RUN_TEST(indexed_by_the_current_process);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
