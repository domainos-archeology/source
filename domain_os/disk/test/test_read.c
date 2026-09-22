/*
 * disk/test/test_read.c - Unit tests for DISK_$READ (0x00E3CF64)
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
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
uint16_t PROC1_$CURRENT = 5;

static int io_calls;
static uint16_t io_op, io_vol;
static uint32_t io_ppn, io_daddr;
static uint32_t *io_info;

status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t ppn, uint32_t daddr,
                  uint32_t *info)
{
    io_calls++;
    io_op = op; io_vol = vol_idx; io_ppn = ppn; io_daddr = daddr; io_info = info;
    return 0x4242;
}

#include "../read.c"

static void reset(void)
{
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    io_calls = 0;
    PROC1_$CURRENT = 5;
}

TEST(mounted_volume_reads)
{
    uint32_t info[8];
    status_$t st = 0;
    reset();
    DISK_VOL(2)->mount_state = DISK_MOUNT_BUSY;
    DISK_$READ(2, 0x123, 0x456, info, &st);
    ASSERT_EQ(1, io_calls);
    ASSERT_EQ(0, io_op);
    ASSERT_EQ(2, io_vol);
    ASSERT_EQ(0x456, io_ppn);
    ASSERT_EQ(0x123, io_daddr);
    ASSERT_EQ((unsigned long)info, (unsigned long)io_info);
    ASSERT_EQ(0x4242, st);
}

TEST(reserved_by_caller_reads)
{
    uint32_t info[8];
    status_$t st = 0;
    reset();
    DISK_VOL(2)->mount_state = DISK_MOUNT_RESERVED;
    DISK_VOL(2)->mount_proc = 5;
    DISK_$READ(2, 1, 2, info, &st);
    ASSERT_EQ(1, io_calls);
}

TEST(reserved_by_other_rejected)
{
    uint32_t info[8];
    status_$t st = 0;
    reset();
    DISK_VOL(2)->mount_state = DISK_MOUNT_RESERVED;
    DISK_VOL(2)->mount_proc = 6;
    DISK_$READ(2, 1, 2, info, &st);
    ASSERT_EQ(0, io_calls);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
}

TEST(assigned_state_rejected)
{
    uint32_t info[8];
    status_$t st = 0;
    reset();
    DISK_VOL(2)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(2)->mount_proc = 5;
    DISK_$READ(2, 1, 2, info, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
}

int main(void)
{
    printf("test_read:\n");
    RUN_TEST(mounted_volume_reads);
    RUN_TEST(reserved_by_caller_reads);
    RUN_TEST(reserved_by_other_rejected);
    RUN_TEST(assigned_state_rejected);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
