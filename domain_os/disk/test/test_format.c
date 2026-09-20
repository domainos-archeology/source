/*
 * disk/test/test_format.c - Unit tests for DISK_$FORMAT (0x00E3D396)
 *
 * disk/format.c is #included below with the queue-block allocator,
 * DISK_$DO_IO and disk_$wait_io mocked, and DISK_VOL() pointed at a host
 * copy of the module data.  The request block lives in an arena that
 * ARCH_HOST_VA_BASE points at.
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "arch/arch.h"

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
uint16_t PROC1_$CURRENT = 3;

static uint8_t arena[0x200];
#define REQ_VA   0x100u
#define TAIL_VA  0x140u

static int qblk_gets, rtns, do_ios, waits;
static void *do_io_vol;
static void *rtn_first, *rtn_last;
static int8_t do_io_queued;
static status_$t do_io_status;
static uint16_t wait_mask;
static int32_t wait_io_val, wait_err_val;

void disk_$get_qblks_internal(int16_t count, int8_t mode, uint32_t *first_out, uint32_t *last_out)
{
    qblk_gets++;
    (void)count; (void)mode;
    *first_out = REQ_VA;
    *last_out = TAIL_VA;
}

void disk_$rtn_qblks_internal(int16_t count, void *first, void *last)
{
    (void)count;
    rtns++;
    rtn_first = first;
    rtn_last = last;
}

void DISK_$DO_IO(void *vol, void *req, void *param_3, void *result)
{
    (void)param_3;
    do_ios++;
    do_io_vol = vol;
    ((disk_io_req_t *)req)->status = do_io_status;
    *(int8_t *)result = do_io_queued;
}

void disk_$wait_io(uint16_t disk_mask, int32_t *io_wait_val, int32_t *error_wait_val)
{
    waits++;
    wait_mask = disk_mask;
    wait_io_val = *io_wait_val;
    wait_err_val = *error_wait_val;
}

#include "../format.c"

static uint8_t dev[0x10];

static void reset(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    memset(arena, 0, sizeof arena);
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    memset(dev, 0, sizeof dev);
    qblk_gets = rtns = do_ios = waits = 0;
    do_io_queued = 0;
    do_io_status = 0;
    PROC1_$CURRENT = 3;
    /* volume 2 assigned to pid 3, 4 heads, partitions 1 -> vol 2, 2 -> vol 5 */
    DISK_VOL(2)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(2)->mount_proc = 3;
    DISK_VOL(2)->dev_info = dev;
    DISK_VOL(2)->num_heads = 4;
    DISK_VOL(2)->part_volx[1] = 2;
    DISK_VOL(2)->part_volx[2] = 5;
    DISK_VOL(2)->part_volx[3] = 0;
}

static disk_io_req_t *req(void) { return (disk_io_req_t *)(arena + REQ_VA); }

TEST(invalid_volume_index)
{
    uint16_t v = 0, c = 1, h = 0;
    status_$t st = 0;
    reset();
    DISK_$FORMAT(&v, &c, &h, &st);
    ASSERT_EQ(status_$invalid_volume_index, st);
    ASSERT_EQ(0, qblk_gets);
}

TEST(not_assigned)
{
    uint16_t v = 2, c = 1, h = 0;
    status_$t st = 0;
    reset();
    DISK_VOL(2)->mount_proc = 4;
    DISK_$FORMAT(&v, &c, &h, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
    ASSERT_EQ(0, qblk_gets);
}

TEST(driver_without_track_format)
{
    uint16_t v = 2, c = 1, h = 0;
    status_$t st = 0;
    reset();
    *(uint16_t *)(dev + 0x08) = 0x0200;
    DISK_$FORMAT(&v, &c, &h, &st);
    ASSERT_EQ(status_$disk_illegal_request_for_device, st);
    ASSERT_EQ(0, qblk_gets);
}

/* head 5 with 4 heads: partition 2 (volume 5), head 1 within it. */
TEST(head_split_across_partition)
{
    uint16_t v = 2, c = 0x1234, h = 5;
    status_$t st = 0;
    reset();
    do_io_status = 0x77;
    *(int32_t *)(DISK_$DATA + 3 * DMOD_PER_PROC_SIZE + DMOD_PER_PROC_IO_EC) = 10;
    *(int32_t *)(DISK_$DATA + 3 * DMOD_PER_PROC_SIZE + DMOD_PER_PROC_ERR_EC) = 20;
    DISK_$FORMAT(&v, &c, &h, &st);
    ASSERT_EQ(1, qblk_gets);
    ASSERT_EQ(1, do_ios);
    ASSERT_EQ((unsigned long)DISK_VOL(5), (unsigned long)do_io_vol);
    ASSERT_EQ(0x12340101, req()->daddr);
    ASSERT_EQ(0x03, req()->op_flags);
    ASSERT_EQ(0, waits);
    ASSERT_EQ(0x77, st);
    ASSERT_EQ(1, rtns);
    ASSERT_EQ((unsigned long)req(), (unsigned long)rtn_first);
    ASSERT_EQ((unsigned long)(arena + TAIL_VA), (unsigned long)rtn_last);
}

/* A queued request waits on the partition volume's bit with ec + 1. */
TEST(queued_request_waits)
{
    uint16_t v = 2, c = 7, h = 2;
    status_$t st = 0;
    reset();
    do_io_queued = -1;
    *(int32_t *)(DISK_$DATA + 3 * DMOD_PER_PROC_SIZE + DMOD_PER_PROC_IO_EC) = 10;
    *(int32_t *)(DISK_$DATA + 3 * DMOD_PER_PROC_SIZE + DMOD_PER_PROC_ERR_EC) = 20;
    DISK_$FORMAT(&v, &c, &h, &st);
    ASSERT_EQ((unsigned long)DISK_VOL(2), (unsigned long)do_io_vol);
    ASSERT_EQ(0x00070201, req()->daddr);
    ASSERT_EQ(1, waits);
    ASSERT_EQ(1u << 2, wait_mask);
    ASSERT_EQ(11, wait_io_val);
    ASSERT_EQ(21, wait_err_val);
}

/* The op nibble replaces the low nibble and keeps the high one. */
TEST(op_flags_high_nibble_kept)
{
    uint16_t v = 2, c = 1, h = 0;
    status_$t st = 0;
    reset();
    req()->op_flags = 0xa9;
    DISK_$FORMAT(&v, &c, &h, &st);
    ASSERT_EQ(0xa3, req()->op_flags);
}

/* Partition entry 3 is volume 0: invalid, and the queue block is NOT
 * returned (0x00E3D48A jumps straight to the exit). */
TEST(invalid_partition_leaks_block)
{
    uint16_t v = 2, c = 1, h = 9;
    status_$t st = 0;
    reset();
    DISK_$FORMAT(&v, &c, &h, &st);
    ASSERT_EQ(status_$invalid_volume_index, st);
    ASSERT_EQ(1, qblk_gets);
    ASSERT_EQ(0, do_ios);
    ASSERT_EQ(0, rtns);
}

TEST(partition_index_above_8_rejected)
{
    uint16_t v = 2, c = 1, h = 32;      /* 32 / 4 + 1 = 9 */
    status_$t st = 0;
    reset();
    DISK_$FORMAT(&v, &c, &h, &st);
    ASSERT_EQ(status_$invalid_volume_index, st);
    ASSERT_EQ(0, do_ios);
    ASSERT_EQ(0, rtns);
}

int main(void)
{
    printf("test_format:\n");
    RUN_TEST(invalid_volume_index);
    RUN_TEST(not_assigned);
    RUN_TEST(driver_without_track_format);
    RUN_TEST(head_split_across_partition);
    RUN_TEST(queued_request_waits);
    RUN_TEST(op_flags_high_nibble_kept);
    RUN_TEST(invalid_partition_leaks_block);
    RUN_TEST(partition_index_above_8_rejected);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
