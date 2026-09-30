/*
 * acl/test/test_override_local_locksmith.c - ACL_$OVERRIDE_LOCAL_LOCKSMITH
 * (0x00E49254): only type-9 processes, the bit copied from the boolean.
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

#include "acl/acl_internal.h"

MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);
uint16_t PROC1_$CURRENT;

MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

#include "../override_local_locksmith.c"

TEST(wrong_type)
{
    status_$t st = 0;
    memset(&ACL_$DATA, 0, sizeof ACL_$DATA);
    PROC1_$CURRENT = 3;
    PROC1_$DATA.type[3] = 8;
    ACL_$OVERRIDE_LOCAL_LOCKSMITH((int8_t)0xFF, &st);
    ASSERT_EQ(status_$no_right_to_perform_operation, st);
    ASSERT_EQ(0, ACL_$DATA.locksmith_override_bitmap[0]);
}

TEST(set_and_clear)
{
    status_$t st = 7;
    memset(&ACL_$DATA, 0, sizeof ACL_$DATA);
    PROC1_$CURRENT = 10;                    /* bit 0x40 of byte 1 */
    PROC1_$DATA.type[10] = 9;
    ACL_$DATA.locksmith_override_bitmap[1] = 0x81;
    ACL_$OVERRIDE_LOCAL_LOCKSMITH((int8_t)0xFF, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0xC1, ACL_$DATA.locksmith_override_bitmap[1]);
    ACL_$OVERRIDE_LOCAL_LOCKSMITH(0, &st);
    ASSERT_EQ(0x81, ACL_$DATA.locksmith_override_bitmap[1]);
}

TEST(partial_byte_copies_own_bit)
{
    status_$t st = 7;
    memset(&ACL_$DATA, 0, sizeof ACL_$DATA);
    PROC1_$CURRENT = 1;                     /* bit 0x80 of byte 0 */
    PROC1_$DATA.type[1] = 9;
    ACL_$OVERRIDE_LOCAL_LOCKSMITH(0x7F, &st);
    ASSERT_EQ(0, ACL_$DATA.locksmith_override_bitmap[0]);
}

int main(void)
{
    printf("ACL_$OVERRIDE_LOCAL_LOCKSMITH tests:\n");
    RUN_TEST(wrong_type);
    RUN_TEST(set_and_clear);
    RUN_TEST(partial_byte_copies_own_bit);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
