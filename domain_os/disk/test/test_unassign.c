/*
 * disk/test/test_unassign.c - Unit tests for DISK_$UNASSIGN (0x00E6BDA8)
 * and DISK_$UNASSIGN_ALL (0x00E6BE1C)
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "network/network.h"
#include "proc1/proc1.h"

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
uint16_t PROC1_$CURRENT = 4;
int8_t NETWORK_$REALLY_DISKLESS;

static int dismounts;
static uint16_t dismounted[16];
void DISK_$DISMOUNT(uint16_t v) { if (dismounts < 16) dismounted[dismounts] = v; dismounts++; }

#include "../unassign.c"
#include "../unassign_all.c"

static void reset(void)
{
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    NETWORK_$REALLY_DISKLESS = 0;
    dismounts = 0;
}

TEST(diskless_rejects_everything)
{
    uint16_t v = 3;
    status_$t st = 0;
    reset();
    NETWORK_$REALLY_DISKLESS = -1;
    DISK_VOL(3)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(3)->mount_proc = 4;
    DISK_$UNASSIGN(&v, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
    ASSERT_EQ(0, dismounts);
}

TEST(invalid_index)
{
    uint16_t v = 0;
    status_$t st = 0;
    reset();
    DISK_$UNASSIGN(&v, &st);
    ASSERT_EQ(status_$invalid_volume_index, st);
}

TEST(assigned_to_me_is_dismounted)
{
    uint16_t v = 3;
    status_$t st = 0x99;
    reset();
    DISK_VOL(3)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(3)->mount_proc = 4;
    DISK_$UNASSIGN(&v, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, dismounts);
    ASSERT_EQ(3, dismounted[0]);
}

TEST(assigned_to_other_rejected)
{
    uint16_t v = 3;
    status_$t st = 0;
    reset();
    DISK_VOL(3)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(3)->mount_proc = 5;
    DISK_$UNASSIGN(&v, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
}

TEST(unassign_all_walks_1_to_10)
{
    reset();
    DISK_VOL(2)->mount_state = DISK_MOUNT_ASSIGNED; DISK_VOL(2)->mount_proc = 4;
    DISK_VOL(10)->mount_state = DISK_MOUNT_ASSIGNED; DISK_VOL(10)->mount_proc = 4;
    DISK_$UNASSIGN_ALL();
    ASSERT_EQ(2, dismounts);
    ASSERT_EQ(2, dismounted[0]);
    ASSERT_EQ(10, dismounted[1]);
}

int main(void)
{
    printf("test_unassign:\n");
    RUN_TEST(diskless_rejects_everything);
    RUN_TEST(invalid_index);
    RUN_TEST(assigned_to_me_is_dismounted);
    RUN_TEST(assigned_to_other_rejected);
    RUN_TEST(unassign_all_walks_1_to_10);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
