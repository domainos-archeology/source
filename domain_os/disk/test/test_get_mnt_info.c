/*
 * disk/test/test_get_mnt_info.c - Unit tests for DISK_$GET_MNT_INFO
 * (0x00E6BE4A)
 *
 * disk/get_mnt_info.c is #included below with ML_$EXCLUSION_* mocked and
 * DISK_VOL() pointed at a host copy of the module data.
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

#include "../get_mnt_info.c"

/* driver records: word +4 type, +6 controller, +8 flags */
static uint8_t dev_a[0x10], dev_b[0x10];

static void set_dev(uint8_t *d, uint16_t type, uint16_t ctrl, uint16_t flags)
{
    memset(d, 0, 0x10);
    *(uint16_t *)(d + 4) = type;
    *(uint16_t *)(d + 6) = ctrl;
    *(uint16_t *)(d + 8) = flags;
}

static void reset(void)
{
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    starts = stops = 0;
    set_dev(dev_a, 0x0021, 0x0003, 0x0000);
    set_dev(dev_b, 0x0022, 0x0005, 0x0000);
    /* PV 1: two partitions, 1 -> itself, 2 -> volume 4 (on dev_b) */
    DISK_VOL(1)->mount_state = DISK_MOUNT_MOUNTED;
    DISK_VOL(1)->dev_info = dev_a;
    DISK_VOL(1)->dev_unit = 0x0002;
    DISK_VOL(1)->unit_id = 0x0102;
    DISK_VOL(1)->addr_start = 0x10;
    DISK_VOL(1)->addr_end = 0x2000;
    DISK_VOL(1)->sec_per_track = 18;
    DISK_VOL(1)->num_heads = 7;
    DISK_VOL(1)->bat_step = 1;
    DISK_VOL(1)->sector_size_code = 1;
    DISK_VOL(1)->num_parts = 2;
    DISK_VOL(1)->part_volx[0] = 0x0303;
    DISK_VOL(1)->part_volx[1] = 1;
    DISK_VOL(1)->part_volx[2] = 4;
    DISK_VOL(4)->dev_info = dev_b;
    DISK_VOL(4)->dev_unit = 0x000b;
    /* LV 2 on PV 1 */
    DISK_VOL(2)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(2)->lv_start = 0x100;
    DISK_VOL(2)->addr_start = 0x5555;
    DISK_VOL(2)->part_volx[1] = 1;
}

TEST(invalid_index_no_lock)
{
    uint16_t v = 12;
    disk_$mnt_info_t info;
    status_$t st = 0;
    reset();
    DISK_$GET_MNT_INFO(&v, NULL, &info, &st);
    ASSERT_EQ(status_$invalid_volume_index, st);
    ASSERT_EQ(0, starts);
}

TEST(unmounted_volume)
{
    uint16_t v = 3;
    disk_$mnt_info_t info;
    status_$t st = 0;
    reset();
    DISK_$GET_MNT_INFO(&v, NULL, &info, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
    ASSERT_EQ(1, starts);
    ASSERT_EQ(1, stops);
}

TEST(physical_volume_fields)
{
    uint16_t v = 1;
    disk_$mnt_info_t info;
    status_$t st = 0x11;
    reset();
    memset(&info, 0xff, sizeof info);
    DISK_$GET_MNT_INFO(&v, NULL, &info, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x10, info.vol_start);
    ASSERT_EQ(0x2000, info.vol_end);
    ASSERT_EQ(0x0021, info.dev_type);
    ASSERT_EQ(0x0102, info.unit_id);
    ASSERT_EQ(1, info.bat_step);
    ASSERT_EQ(18, info.sectors_per_track);
    ASSERT_EQ(7, info.heads);
    ASSERT_EQ(2, info.sectors_per_block);
    ASSERT_EQ(2, info.n_partitions);
    ASSERT_EQ(0x0303, info.interleave);
    /* partition 1: type 0x21 << 3 = 0x08 (byte), low 3 bits <- ctrl 3;
     * unit 2 << 4 */
    ASSERT_EQ(0x0b20, info.part_info[0]);
    /* partition 2: dev_b controller 5, unit 0xb << 4 = 0xb0 */
    ASSERT_EQ(0x0db0, info.part_info[1]);
    ASSERT_EQ(0, info.part_info[2]);
    ASSERT_EQ(0, info.part_info[7]);
    /* flags: mounted (0x80), driver flags >= 0 (0x20), LV bit cleared,
     * the preset 0xff loses bit 0; byte +0x29 cleared */
    ASSERT_EQ(0xa0, info.flags);
    ASSERT_EQ(0, info._pad_29);
    ASSERT_EQ(1, stops);
}

/* An LV reports its own vol_start but the backing PV's other fields. */
TEST(logical_volume_resolves_to_pv)
{
    uint16_t v = 2;
    disk_$mnt_info_t info;
    status_$t st = 0;
    reset();
    memset(&info, 0, sizeof info);
    DISK_$GET_MNT_INFO(&v, NULL, &info, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x5555, info.vol_start);
    ASSERT_EQ(0x2000, info.vol_end);
    ASSERT_EQ(0x0021, info.dev_type);
    ASSERT_EQ(2, info.n_partitions);
    /* LV bit, PV mounted, driver flags >= 0 */
    ASSERT_EQ(0xe0, info.flags);
}

TEST(driver_flag_bits)
{
    uint16_t v = 1;
    disk_$mnt_info_t info;
    status_$t st = 0;
    reset();
    memset(&info, 0, sizeof info);
    set_dev(dev_a, 0x0021, 0x0003, 0x8000 | 0x2000 | 0x0800 | 0x0200);
    DISK_VOL(1)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(1)->as_options = DISK_VOL_FLAG_WRITE_PROTECT;
    DISK_$GET_MNT_INFO(&v, NULL, &info, &st);
    /* not mounted, flags negative -> no 0x20, wp 0x10, 0x04, 0x08, 0x02 */
    ASSERT_EQ(0x1e, info.flags);
}

TEST(unknown_sector_size_code_leaves_field)
{
    uint16_t v = 1;
    disk_$mnt_info_t info;
    status_$t st = 0;
    reset();
    memset(&info, 0, sizeof info);
    info.sectors_per_block = 0x4242;
    DISK_VOL(1)->sector_size_code = 5;
    DISK_$GET_MNT_INFO(&v, NULL, &info, &st);
    ASSERT_EQ(0x4242, info.sectors_per_block);
}

int main(void)
{
    printf("test_get_mnt_info:\n");
    RUN_TEST(invalid_index_no_lock);
    RUN_TEST(unmounted_volume);
    RUN_TEST(physical_volume_fields);
    RUN_TEST(logical_volume_resolves_to_pv);
    RUN_TEST(driver_flag_bits);
    RUN_TEST(unknown_sector_size_code_leaves_field);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
