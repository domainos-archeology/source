/*
 * disk/test/test_get_drte.c - Unit tests for DISK_$GET_DRTE (0x00E3DA1C)
 *
 * disk/get_drte.c is #included below with a host copy of DISK_$DEVICES.
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
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

disk_device_entry_t DISK_$DEVICES[DISK_MAX_DEVICES];

#include "../get_drte.c"

static uint8_t jt_a, jt_b;

static void reset(void)
{
    memset(DISK_$DEVICES, 0, sizeof DISK_$DEVICES);
}

static void reg(int i, void *jt, uint16_t type, uint16_t ctrl)
{
    DISK_$DEVICES[i].jump_table = jt;
    DISK_$DEVICES[i].device_type = type;
    DISK_$DEVICES[i].controller = ctrl;
}

TEST(empty_table_returns_null)
{
    uint16_t t = 1, c = 0;
    reset();
    ASSERT_EQ(0, (unsigned long)DISK_$GET_DRTE(&t, &c));
}

TEST(finds_matching_entry)
{
    uint16_t t = 3, c = 1;
    reset();
    reg(0, &jt_a, 3, 0);
    reg(5, &jt_b, 3, 1);
    ASSERT_EQ((unsigned long)&DISK_$DEVICES[5], (unsigned long)DISK_$GET_DRTE(&t, &c));
}

/* An entry with no jump table is skipped even if type/controller match. */
TEST(unregistered_slot_skipped)
{
    uint16_t t = 3, c = 1;
    reset();
    reg(2, NULL, 3, 1);
    reg(4, &jt_a, 3, 1);
    ASSERT_EQ((unsigned long)&DISK_$DEVICES[4], (unsigned long)DISK_$GET_DRTE(&t, &c));
}

TEST(first_match_wins)
{
    uint16_t t = 7, c = 2;
    reset();
    reg(9, &jt_a, 7, 2);
    reg(10, &jt_b, 7, 2);
    ASSERT_EQ((unsigned long)&DISK_$DEVICES[9], (unsigned long)DISK_$GET_DRTE(&t, &c));
}

TEST(last_slot_is_searched)
{
    uint16_t t = 7, c = 2;
    reset();
    reg(31, &jt_a, 7, 2);
    ASSERT_EQ((unsigned long)&DISK_$DEVICES[31], (unsigned long)DISK_$GET_DRTE(&t, &c));
}

TEST(controller_mismatch_is_null)
{
    uint16_t t = 7, c = 3;
    reset();
    reg(1, &jt_a, 7, 2);
    ASSERT_EQ(0, (unsigned long)DISK_$GET_DRTE(&t, &c));
}

int main(void)
{
    printf("test_get_drte:\n");
    RUN_TEST(empty_table_returns_null);
    RUN_TEST(finds_matching_entry);
    RUN_TEST(unregistered_slot_skipped);
    RUN_TEST(first_match_wins);
    RUN_TEST(last_slot_is_searched);
    RUN_TEST(controller_mismatch_is_null);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
