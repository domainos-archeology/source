/*
 * disk/test/test_lvuid_to_volx.c - Unit tests for DISK_$LVUID_TO_VOLX
 * (0x00E6D134)
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "ml/ml.h"

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

uint8_t DISK_$DATA[DISK_$DATA_SIZE];
#undef DISK_VOLUME_BASE
#define DISK_VOLUME_BASE (DISK_$DATA)
ml_$exclusion_t MOUNT_LOCK;

static int starts, stops;
void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; starts++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { (void)e; stops++; }

#include "../lvuid_to_volx.c"

static void reset(void)
{
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    starts = stops = 0;
}

static void lv(int idx, uint16_t state, uint32_t start, uint32_t hi, uint32_t lo)
{
    DISK_VOL(idx)->mount_state = state;
    DISK_VOL(idx)->lv_start = start;
    DISK_VOL(idx)->lv_uid.high = hi;
    DISK_VOL(idx)->lv_uid.low = lo;
}

TEST(finds_mounted_lv)
{
    uid_t u = { 0xAAAA, 0xBBBB };
    int16_t idx = 0;
    status_$t st = 0;
    reset();
    lv(2, DISK_MOUNT_MOUNTED, 0x100, 0x1111, 0x2222);
    lv(5, DISK_MOUNT_MOUNTED, 0x200, 0xAAAA, 0xBBBB);
    DISK_$LVUID_TO_VOLX(&u, &idx, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(5, idx);
    ASSERT_EQ(1, starts);
    ASSERT_EQ(1, stops);
}

/* A PV (lv_start 0) or an assigned-not-mounted LV is never a candidate. */
TEST(skips_pv_and_unmounted)
{
    uid_t u = { 0xAAAA, 0xBBBB };
    int16_t idx = 0;
    status_$t st = 0;
    reset();
    lv(1, DISK_MOUNT_MOUNTED, 0, 0xAAAA, 0xBBBB);
    lv(2, DISK_MOUNT_ASSIGNED, 0x100, 0xAAAA, 0xBBBB);
    DISK_$LVUID_TO_VOLX(&u, &idx, &st);
    ASSERT_EQ(status_$logical_volume_not_found, st);
    ASSERT_EQ(1, idx);
}

/* Only descriptors 1..6 are scanned. */
TEST(volume_7_not_scanned)
{
    uid_t u = { 0xAAAA, 0xBBBB };
    int16_t idx = 0;
    status_$t st = 0;
    reset();
    lv(7, DISK_MOUNT_MOUNTED, 0x100, 0xAAAA, 0xBBBB);
    DISK_$LVUID_TO_VOLX(&u, &idx, &st);
    ASSERT_EQ(status_$logical_volume_not_found, st);
}

TEST(first_match_wins)
{
    uid_t u = { 0xAAAA, 0xBBBB };
    int16_t idx = 0;
    status_$t st = 0;
    reset();
    lv(3, DISK_MOUNT_MOUNTED, 0x100, 0xAAAA, 0xBBBB);
    lv(4, DISK_MOUNT_MOUNTED, 0x200, 0xAAAA, 0xBBBB);
    DISK_$LVUID_TO_VOLX(&u, &idx, &st);
    ASSERT_EQ(3, idx);
}

int main(void)
{
    printf("test_lvuid_to_volx:\n");
    RUN_TEST(finds_mounted_lv);
    RUN_TEST(skips_pv_and_unmounted);
    RUN_TEST(volume_7_not_scanned);
    RUN_TEST(first_match_wins);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
