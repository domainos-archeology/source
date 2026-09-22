/*
 * disk/test/test_write_protect.c - Unit tests for DISK_$WRITE_PROTECT
 * (0x00E3D956) and DISK_$WRITE (0x00E3CC78)
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

uint8_t DISK_$DATA[DISK_$DATA_SIZE];
#undef DISK_VOLUME_BASE
#define DISK_VOLUME_BASE (DISK_$DATA)

static int io_calls;
static uint16_t io_op;
status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t ppn, uint32_t daddr,
                  uint32_t *info)
{
    (void)vol_idx; (void)ppn; (void)daddr; (void)info;
    io_calls++; io_op = op;
    return 0x77;
}

#include "../write_protect.c"
#include "../write.c"

TEST(set_and_test)
{
    status_$t st = 0x11;
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    DISK_VOL(2)->as_options = 0x0100;
    DISK_$WRITE_PROTECT(1, 2, &st);
    ASSERT_EQ(0, st);
    DISK_$WRITE_PROTECT(0, 2, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x0101, DISK_VOL(2)->as_options);
    DISK_$WRITE_PROTECT(1, 2, &st);
    ASSERT_EQ(status_$disk_write_protected, st);
    st = 0x11;
    DISK_$WRITE_PROTECT(5, 2, &st);         /* other modes only clear status */
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x0101, DISK_VOL(2)->as_options);
}

TEST(write_requires_mounted)
{
    uint32_t info[8];
    status_$t st = 0;
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    io_calls = 0;
    DISK_VOL(3)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_$WRITE(3, 1, 2, info, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
    ASSERT_EQ(0, io_calls);
    DISK_VOL(3)->mount_state = DISK_MOUNT_BUSY;
    DISK_$WRITE(3, 1, 2, info, &st);
    ASSERT_EQ(1, io_calls);
    ASSERT_EQ(1, io_op);
    ASSERT_EQ(0x77, st);
}

int main(void)
{
    printf("test_write_protect:\n");
    RUN_TEST(set_and_test);
    RUN_TEST(write_requires_mounted);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
