/*
 * pbu/test/test_pbu.c - PBU_$ADVANCE_EC_INT (0x00E88400) and the three PBU
 * stubs (0x00E590F8 / 0x00E590FA / 0x00E5910E)
 *
 * The record pool EC2_$PBU_ECS is 32 x 0x18 bytes on the m68k; on a 64-bit
 * host the record is wider (its eventcount holds two pointers), so the
 * tests only touch records that still fit in the pool's byte size.
 */

#include <stdio.h>
#include <string.h>

#include "pbu/pbu_internal.h"

uint8_t EC2_$PBU_ECS[EC2_PBU_EC_COUNT * EC2_PBU_EC_SIZE];

static int advance_calls; static ec_$eventcount_t *advance_arg;
void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec) { advance_calls++; advance_arg = ec; }

#include "../advance_ec_int.c"
#include "../faulted_units.c"
#include "../free_asid.c"
#include "../init.c"

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

#define POISON 0x5a5a5a5a

TEST(index_below_range_is_rejected)
{
    status_$t st = POISON; int16_t owner = 1; uint32_t idx = 0x100;
    advance_calls = 0;
    PBU_$ADVANCE_EC_INT(&owner, &idx, &st);
    ASSERT_EQ(0x00180004, st);
    ASSERT_EQ(0, advance_calls);
}

TEST(index_above_range_is_rejected)
{
    status_$t st = POISON; int16_t owner = 1; uint32_t idx = 0x121;
    advance_calls = 0;
    PBU_$ADVANCE_EC_INT(&owner, &idx, &st);
    ASSERT_EQ(0x00180004, st);
    ASSERT_EQ(0, advance_calls);
}

TEST(huge_index_is_unsigned_compare)
{
    status_$t st = POISON; int16_t owner = 1; uint32_t idx = 0xFFFF0110u;
    advance_calls = 0;
    PBU_$ADVANCE_EC_INT(&owner, &idx, &st);   /* bhi: 0xFFFF0110 > 0x120 unsigned */
    ASSERT_EQ(0x00180004, st);
    ASSERT_EQ(0, advance_calls);
}

TEST(owner_mismatch_is_rejected)
{
    status_$t st = POISON; int16_t owner = 0x1234; uint32_t idx = 0x101;
    memset(EC2_$PBU_ECS, 0, sizeof EC2_$PBU_ECS);
    PBU_$EC_ARRAY[0].owner_id = 0x1235;
    advance_calls = 0;
    PBU_$ADVANCE_EC_INT(&owner, &idx, &st);
    ASSERT_EQ(0x00180004, st);
    ASSERT_EQ(0, advance_calls);
}

TEST(matching_owner_advances_record)
{
    status_$t st = POISON; int16_t owner = -2; uint32_t idx = 0x101 + 5;
    memset(EC2_$PBU_ECS, 0, sizeof EC2_$PBU_ECS);
    PBU_$EC_ARRAY[5].owner_id = -2;
    advance_calls = 0;
    PBU_$ADVANCE_EC_INT(&owner, &idx, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((unsigned long long)(uintptr_t)&PBU_$EC_ARRAY[5].ec,
              (unsigned long long)(uintptr_t)advance_arg);
}

TEST(stubs)
{
    status_$t st = POISON;
    ASSERT_EQ(0, PBU_$FAULTED_UNITS(&st));
    ASSERT_EQ(0x001e000a, st);
    PBU_$FREE_ASID();
    PBU_$INIT();
}

int main(void)
{
    printf("PBU tests\n");
    RUN_TEST(index_below_range_is_rejected);
    RUN_TEST(index_above_range_is_rejected);
    RUN_TEST(huge_index_is_unsigned_compare);
    RUN_TEST(owner_mismatch_is_rejected);
    RUN_TEST(matching_owner_advances_record);
    RUN_TEST(stubs);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
