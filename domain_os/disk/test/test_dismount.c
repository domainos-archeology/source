/*
 * disk/test/test_dismount.c - Unit tests for DISK_$DISMOUNT (0x00E6CFEA)
 *
 * disk/dismount.c is #included below with ML_$EXCLUSION_*, DISK_$INVALIDATE,
 * DISK_$SHUTDOWN and CRASH_SYSTEM mocked, and DISK_VOL() pointed at a host
 * copy of the module data.
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "disk/disk_internal.h"
#include "misc/misc.h"

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

MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);

static int starts, stops, shutdowns;
static int invalidates;
static uint16_t invalidated[16];
static void *shutdown_dev;
static uint16_t shutdown_unit;
static jmp_buf crash_jmp;
static status_$t crash_status;

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; starts++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { (void)e; stops++; }
void DISK_$INVALIDATE(uint16_t v) { if (invalidates < 16) invalidated[invalidates] = v; invalidates++; }
void DISK_$SHUTDOWN(disk_device_entry_t *d, uint16_t u) { shutdowns++; shutdown_dev = d; shutdown_unit = u; }
void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }

#include "../dismount.c"

static uint8_t dev_a, dev_b;

static void reset(void)
{
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    starts = stops = shutdowns = invalidates = 0;
    shutdown_dev = NULL;
    crash_status = 0;
}

static void vol(int idx, uint16_t state, void *dev, uint16_t unit, uint32_t lv_start)
{
    disk_$volume_t *v = DISK_VOL(idx);
    v->mount_state = state;
    v->dev_info = dev;
    v->dev_unit = unit;
    v->lv_start = lv_start;
}

TEST(invalid_index_does_nothing)
{
    reset();
    DISK_$DISMOUNT(0);
    DISK_$DISMOUNT(11);
    ASSERT_EQ(0, starts);
    ASSERT_EQ(0, invalidates);
}

TEST(unmounted_volume_only_invalidates)
{
    reset();
    vol(3, 0, &dev_a, 0, 0);
    DISK_$DISMOUNT(3);
    ASSERT_EQ(1, starts);
    ASSERT_EQ(1, stops);
    ASSERT_EQ(1, invalidates);
    ASSERT_EQ(3, invalidated[0]);
    ASSERT_EQ(0, shutdowns);
}

/* LV 2 on PV 1; dismounting the LV clears its state, then the PV has no
 * more LVs so the drive is shut down and the PV's one partition (itself)
 * is invalidated and unmounted. */
TEST(last_lv_dismount_shuts_drive_down)
{
    reset();
    vol(1, DISK_MOUNT_MOUNTED, &dev_a, 0, 0);
    DISK_VOL(1)->num_parts = 1;
    DISK_VOL(1)->part_volx[1] = 1;
    vol(2, DISK_MOUNT_MOUNTED, &dev_a, 0, 0x1000);

    DISK_$DISMOUNT(2);

    ASSERT_EQ(0, DISK_VOL(2)->mount_state);
    ASSERT_EQ(1, shutdowns);
    ASSERT_EQ((unsigned long)&dev_a, (unsigned long)shutdown_dev);
    ASSERT_EQ(2, invalidates);
    ASSERT_EQ(2, invalidated[0]);
    ASSERT_EQ(1, invalidated[1]);
    ASSERT_EQ(0, DISK_VOL(1)->mount_state);
    ASSERT_EQ(1, stops);
}

/* A second LV on the same drive keeps it up. */
TEST(other_lv_keeps_drive_up)
{
    reset();
    vol(1, DISK_MOUNT_MOUNTED, &dev_a, 0, 0);
    vol(2, DISK_MOUNT_MOUNTED, &dev_a, 0, 0x1000);
    vol(3, DISK_MOUNT_ASSIGNED, &dev_a, 0, 0x2000);

    DISK_$DISMOUNT(2);

    ASSERT_EQ(0, DISK_VOL(2)->mount_state);
    ASSERT_EQ(0, shutdowns);
    ASSERT_EQ(1, invalidates);
    ASSERT_EQ(DISK_MOUNT_MOUNTED, DISK_VOL(1)->mount_state);
}

/* An LV on a different unit of the same controller does not count. */
TEST(different_unit_does_not_count)
{
    reset();
    vol(1, DISK_MOUNT_MOUNTED, &dev_a, 0, 0);
    DISK_VOL(1)->num_parts = 1;
    DISK_VOL(1)->part_volx[1] = 1;
    vol(2, DISK_MOUNT_MOUNTED, &dev_a, 0, 0x1000);
    vol(3, DISK_MOUNT_MOUNTED, &dev_a, 1, 0x2000);
    vol(4, DISK_MOUNT_MOUNTED, &dev_b, 0, 0x3000);

    DISK_$DISMOUNT(2);

    ASSERT_EQ(1, shutdowns);
    ASSERT_EQ(0, shutdown_unit);
    ASSERT_EQ(DISK_MOUNT_MOUNTED, DISK_VOL(3)->mount_state);
    ASSERT_EQ(DISK_MOUNT_MOUNTED, DISK_VOL(4)->mount_state);
}

/* Dismounting a PV directly: its state is not cleared by the first step
 * (lv_start == 0), but the partition walk clears it.  Two PV descriptors
 * share the drive here; the scan keeps the LAST one it finds (0x00E6D098
 * overwrites D2), so the partition table is read from volume 5. */
TEST(pv_dismount_walks_partitions)
{
    reset();
    vol(1, DISK_MOUNT_ASSIGNED, &dev_a, 2, 0);
    vol(5, DISK_MOUNT_ASSIGNED, &dev_a, 2, 0);
    DISK_VOL(5)->num_parts = 2;
    DISK_VOL(5)->part_volx[1] = 1;
    DISK_VOL(5)->part_volx[2] = 5;

    DISK_$DISMOUNT(1);

    ASSERT_EQ(1, shutdowns);
    ASSERT_EQ(2, shutdown_unit);
    ASSERT_EQ(3, invalidates);
    ASSERT_EQ(1, invalidated[1]);
    ASSERT_EQ(5, invalidated[2]);
    ASSERT_EQ(0, DISK_VOL(1)->mount_state);
    ASSERT_EQ(0, DISK_VOL(5)->mount_state);
}

/* An LV whose drive has no PV descriptor is a driver logic error. */
TEST(missing_pv_crashes)
{
    reset();
    vol(2, DISK_MOUNT_MOUNTED, &dev_a, 0, 0x1000);
    if (setjmp(crash_jmp) == 0) {
        DISK_$DISMOUNT(2);
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(0x00080022, crash_status);
    ASSERT_EQ(0, shutdowns);
}

int main(void)
{
    printf("test_dismount:\n");
    RUN_TEST(invalid_index_does_nothing);
    RUN_TEST(unmounted_volume_only_invalidates);
    RUN_TEST(last_lv_dismount_shuts_drive_down);
    RUN_TEST(other_lv_keeps_drive_up);
    RUN_TEST(different_unit_does_not_count);
    RUN_TEST(pv_dismount_walks_partitions);
    RUN_TEST(missing_pv_crashes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
