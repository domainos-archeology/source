/*
 * disk/test/test_io_error.c - unit tests for disk_$io_error (0x00e3c14c)
 *
 * The real disk/io_error.c is #included below and driven against a mock
 * DISK_$DATA area with LOG_$ADD, CRASH_SYSTEM and the Domain Pascal multiply
 * helper mocked out.
 */

#include "disk/disk_internal.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * Test harness
 * ================================================================ */
static int tests_failed = 0;
static int tests_run = 0;

#define RUN_TEST(name) do {                     \
    tests_run++;                                \
    printf("  Running %s... ", #name);          \
    fflush(stdout);                             \
    if (test_##name() == 0) printf("PASSED\n"); \
} while (0)

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        printf("FAILED\n    %s at %s:%d\n", #cond, __FILE__,        \
               __LINE__);                                           \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

#define CHECK_EQ(expected, actual) do {                             \
    unsigned long e_ = (unsigned long)(expected);                   \
    unsigned long a_ = (unsigned long)(actual);                     \
    if (e_ != a_) {                                                 \
        printf("FAILED\n    %s: expected 0x%lx, got 0x%lx at %s:%d\n", \
               #actual, e_, a_, __FILE__, __LINE__);                \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

/* ================================================================
 * Globals and the DISK_$DATA override
 * ================================================================ */
static uint8_t mock_disk_data[0xB00];
#undef DISK_VOLUME_BASE
#define DISK_VOLUME_BASE (mock_disk_data)

ml_$exclusion_t ml_$exclusion_t_00e7a274;
ml_$exclusion_t MOUNT_LOCK;
uint32_t TIME_$CURRENT_CLOCKH;
uint32_t TIME_$CLOCKH;

/* ---- mocks ---- */
static int log_calls;
static int16_t log_type;
static int16_t log_len;
static disk_$error_log_t log_rec;

void LOG_$ADD(int16_t type, void *data, int16_t data_len)
{
    log_calls++;
    log_type = type;
    log_len = data_len;
    memcpy(&log_rec, data, sizeof(log_rec));
}

static int crash_calls;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *status)
{
    crash_calls++;
    crash_status = *status;
}

/* 0x00E0AC02: an unsigned 32x16 multiply truncated to 32 bits */
ulong M$MIU$LLW(ulong multiplicand, ushort multiplier)
{
    return (ulong)((uint32_t)multiplicand * (uint32_t)multiplier);
}

#include "../io_error.c"

/* DISK_$DIAG, DISK_$DO_CHKSUM and the module exclusion lock are cells of the
 * DISK_ module block (disk/disk.h), so the host build provides the block. */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/* ================================================================
 * Fixtures
 * ================================================================ */
static uint8_t mock_dev_info[16];
static disk_io_req_t req;
static uint32_t caller_info[8];

static disk_$volume_t *vol_of(int idx)
{
    return DISK_VOL(idx);
}

static void reset_fixture(uint16_t dev_flags)
{
    disk_$volume_t *v;
    int i;

    memset(mock_disk_data, 0, sizeof(mock_disk_data));
    memset(mock_dev_info, 0, sizeof(mock_dev_info));
    memset(&req, 0, sizeof(req));
    memset(&log_rec, 0, sizeof(log_rec));
    log_calls = 0;
    crash_calls = 0;
    crash_status = 0;
    TIME_$CURRENT_CLOCKH = 0xAABBCCDDu;

    for (i = 0; i < 8; i++) {
        caller_info[i] = 0x1000u + (uint32_t)i;
        req.header[i] = 0x2000u + (uint32_t)i;
    }

    *(uint16_t *)(mock_dev_info + 8) = dev_flags;
    mock_dev_info[5] = 2;
    mock_dev_info[7] = 3;

    v = vol_of(1);
    v->dev_info = mock_dev_info;
    v->dev_unit = 0x0005;
    v->sec_per_track = 12;
    v->num_heads = 5;
    v->sector_size_code = 0;
    v->blocks_per_cyl = 60;
    v->part_volx[0] = 0;
    v->part_volx[1] = 1; /* the volume is its own physical volume */

    req.status = 0x00080001; /* something worth logging */
    req.ppn = 0x00004321;
}

/* ================================================================
 * Tests
 * ================================================================ */

/* 0x00E3C160 / 0x00E3C16A: two statuses are never logged */
static int test_ignored_statuses(void)
{
    reset_fixture(0);
    req.status = (status_$t)-1;
    disk_$io_error(1, &req, caller_info);
    CHECK_EQ(0, log_calls);

    reset_fixture(0);
    req.status = status_$invalid_disk_address;
    disk_$io_error(1, &req, caller_info);
    CHECK_EQ(0, log_calls);
    return 0;
}

/*
 * The CHS path.  Cylinder 16, head 3, sector 4 on a volume with 60 blocks per
 * cylinder and 12 sectors per track is block 16*60 + 3*12 + 4 = 1000.
 */
static int test_chs_reconstruction(void)
{
    int i;

    reset_fixture(0);
    req.daddr = (16u << 16) | (3u << 8) | 4u;

    disk_$io_error(1, &req, caller_info);

    CHECK_EQ(1, log_calls);
    CHECK_EQ(DISK_LOG_TYPE_IO_ERROR, log_type);
    CHECK_EQ(16, log_len);
    CHECK_EQ(1000, log_rec.daddr);
    CHECK_EQ(1000, log_rec.block);
    CHECK_EQ(0x00080001, log_rec.status);

    /*
     * 0x00E3C210-0x00E3C27A: the packed device id is
     * ((dev_info[5] << 3) & 0xF8) | dev_info[7] in the high byte and
     * (dev_unit & 0xFF) << 4 in the low byte.
     */
    CHECK_EQ(0x1350, log_rec.pv_devid);
    CHECK_EQ(0x1350, log_rec.vol_devid);

    /* 0x00E3C27C-0x00E3C2BC: the module error block */
    CHECK_EQ(0xAABBCCDDu, DISK_$ERROR_INFO.timestamp);
    CHECK_EQ(1000, DISK_$ERROR_INFO.daddr);
    CHECK_EQ((12u << 16) | 5u, DISK_$ERROR_INFO.geometry);
    CHECK_EQ(1, DISK_$ERROR_INFO.vol_idx);
    CHECK_EQ(0x00004321, DISK_$ERROR_INFO.ppn);
    CHECK_EQ(0x00080001, DISK_$ERROR_INFO.status);
    for (i = 0; i < 8; i++) {
        CHECK_EQ(0x1000u + (uint32_t)i, DISK_$ERROR_INFO.info[i]);
        CHECK_EQ(0x2000u + (uint32_t)i, DISK_$ERROR_INFO.header[i]);
    }
    return 0;
}

/*
 * 0x00E3C184-0x00E3C192: when the caller's volume is not the physical volume
 * behind it, the partition offset is num_parts - 1 and it moves the address.
 */
static int test_partition_offset(void)
{
    disk_$volume_t *v1 = NULL;
    disk_$volume_t *v2 = NULL;

    reset_fixture(0);
    v1 = vol_of(1);
    v2 = vol_of(2);

    v1->part_volx[1] = 2;  /* volume 2 is the physical volume */
    v1->num_parts = 4;     /* -> partition offset 3 */

    /* volume 2 carries the geometry the reconstruction uses */
    v2->dev_info = mock_dev_info;
    v2->dev_unit = 0x0006;
    v2->sec_per_track = 12;
    v2->num_heads = 5;
    v2->sector_size_code = 0;
    v2->blocks_per_cyl = 60;
    v2->stripe_blk_mask = 1;
    v2->stripe_blk_shift = 1;
    v2->stripe_vol_shift = 1;

    req.daddr = (1u << 16) | (0u << 8) | 0u; /* cylinder 1, head 0, sector 0 */

    disk_$io_error(1, &req, caller_info);

    /*
     * blk_in_cyl = 0
     * group      = (1 << 1) + (3 >> 1) = 3
     * daddr      = 3 * 60 + 0 = 180, << 1 = 360, + (3 & 1) = 361
     * block      = 1 * 60 + 0 = 60
     */
    CHECK_EQ(1, log_calls);
    CHECK_EQ(361, log_rec.daddr);
    CHECK_EQ(60, log_rec.block);
    /* the two device ids differ: unit 5 for the caller, unit 6 for the PV */
    CHECK_EQ(0x1350, log_rec.vol_devid);
    CHECK_EQ(0x1360, log_rec.pv_devid);
    return 0;
}

/* 0x00E3C2C8-0x00E3C33A: a linear-address device logs the address as it is */
static int test_linear_device(void)
{
    reset_fixture(DEV_FLAG_LINEAR_ADDRESS);
    req.daddr = 0x00012345;

    disk_$io_error(1, &req, caller_info);

    CHECK_EQ(0, crash_calls);
    CHECK_EQ(1, log_calls);
    CHECK_EQ(0x00012345, log_rec.daddr);
    CHECK_EQ(0x00012345, log_rec.block);
    /* 0x00E3C330: both device ids are the same word on this path */
    CHECK_EQ(log_rec.pv_devid, log_rec.vol_devid);
    CHECK_EQ(0x1350, log_rec.pv_devid);

    /* the geometry, info and header parts of the error block are untouched */
    CHECK_EQ(0, DISK_$ERROR_INFO.geometry);
    CHECK_EQ(0, DISK_$ERROR_INFO.info[0]);
    CHECK_EQ(0x00012345, DISK_$ERROR_INFO.daddr);
    CHECK_EQ(1, DISK_$ERROR_INFO.vol_idx);
    return 0;
}

/* 0x00E3C2CE: a striped linear-address device crashes the system */
static int test_linear_device_striped_crashes(void)
{
    reset_fixture(DEV_FLAG_LINEAR_ADDRESS);
    vol_of(1)->part_volx[0] = 4;
    req.daddr = 0x00012345;

    disk_$io_error(1, &req, caller_info);

    CHECK_EQ(1, crash_calls);
    CHECK_EQ(status_$disk_striping_not_supported, crash_status);
    CHECK_EQ(0, log_calls);
    return 0;
}

/*
 * 0x00E3C1C4: the sector size code shifts hardware sectors down to blocks, so
 * a code of 1 halves the block offset within the cylinder.
 */
static int test_sector_size_code(void)
{
    reset_fixture(0);
    vol_of(1)->sector_size_code = 1;
    req.daddr = (16u << 16) | (3u << 8) | 4u;

    disk_$io_error(1, &req, caller_info);

    /* blk_in_cyl = (3*12 + 4) >> 1 = 20 -> 16*60 + 20 = 980 */
    CHECK_EQ(1, log_calls);
    CHECK_EQ(980, log_rec.daddr);
    CHECK_EQ(980, log_rec.block);
    return 0;
}

int main(void)
{
    printf("disk_$io_error (0x00e3c14c) tests\n");

    RUN_TEST(ignored_statuses);
    RUN_TEST(chs_reconstruction);
    RUN_TEST(partition_offset);
    RUN_TEST(linear_device);
    RUN_TEST(linear_device_striped_crashes);
    RUN_TEST(sector_size_code);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
