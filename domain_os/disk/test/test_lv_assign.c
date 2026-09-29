/*
 * disk/test/test_lv_assign.c - Unit tests for DISK_$LV_ASSIGN (0x00E6CDB2)
 *
 * disk/lv_assign.c is #included below with DISK_$GET_BLOCK, DISK_$SET_BUFF,
 * ML_$EXCLUSION_* and PROC2_$SET_CLEANUP mocked, and DISK_VOL() pointed at
 * a host copy of the module data.
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "uid/uid.h"

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
uint16_t PROC1_$CURRENT = 9;
MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);
uid_t PV_LABEL_$UID = { 0x11, 0x22 };

static int starts, stops, cleanups, set_buffs;
static int16_t cleanup_arg;
static uint8_t label[0x400];
static status_$t get_block_status;
static int16_t get_block_vol;
static int32_t get_block_daddr;
static uid_t *get_block_uid;
static uint16_t get_block_flags;
static uint16_t set_buff_flags;
static void *set_buff_cell;

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; starts++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { (void)e; stops++; }
void PROC2_$SET_CLEANUP(uint16_t v) { cleanups++; cleanup_arg = (int16_t)v; }

void *DISK_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    (void)block_hint; (void)block_type;
    get_block_vol = vol_idx;
    get_block_daddr = daddr;
    get_block_uid = (uid_t *)uid;
    get_block_flags = flags;
    *status = get_block_status;
    return label;
}

void DISK_$SET_BUFF(void *buffer, uint16_t flags, status_$t *param_3)
{
    (void)buffer;
    set_buffs++;
    set_buff_flags = flags;
    set_buff_cell = param_3;
}

#include "../lv_assign.c"

static uint8_t dev_a;

static void reset(void)
{
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    memset(label, 0, sizeof label);
    starts = stops = cleanups = set_buffs = 0;
    get_block_status = 0;
    set_buff_cell = NULL;
    PROC1_$CURRENT = 9;
    /* PV 3 assigned to pid 9, addr_start 0x5000 */
    DISK_VOL(3)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(3)->mount_proc = 9;
    DISK_VOL(3)->dev_info = &dev_a;
    DISK_VOL(3)->dev_unit = 2;
    DISK_VOL(3)->addr_start = 0x5000;
    DISK_VOL(3)->addr_end = 0x6000;
    DISK_VOL(3)->num_heads = 5;
    /* label: LV1 at 0x100, LV2 at 0x1000, LV3 absent; LV2 end 0x3000 */
    *(uint32_t *)(label + 0x38 + 1 * 4) = 0x100;
    *(uint32_t *)(label + 0x38 + 2 * 4) = 0x1000;
    *(uint32_t *)(label + 0x60 + 2 * 4) = 0x3000;
}

TEST(invalid_volume_index_no_lock)
{
    uint16_t v = 11, lv = 1;
    int32_t avail = 0x7777;
    status_$t st = 0;
    reset();
    DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(status_$invalid_volume_index, st);
    ASSERT_EQ(0, starts);
    ASSERT_EQ(0x7777, avail);     /* not written on this path */
}

TEST(invalid_lv_index)
{
    uint16_t v = 3, lv = 0;
    int32_t avail = 0;
    status_$t st = 0;
    reset();
    DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(status_$invalid_logical_volume_index, st);
    lv = 11;
    DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(status_$invalid_logical_volume_index, st);
    ASSERT_EQ(0, cleanups);
}

TEST(not_assigned_to_caller)
{
    uint16_t v = 3, lv = 1;
    int32_t avail = 0;
    status_$t st = 0;
    uint16_t r;
    reset();
    DISK_VOL(3)->mount_proc = 4;
    r = DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
    ASSERT_EQ(1, cleanups);
    ASSERT_EQ(5, cleanup_arg);
    ASSERT_EQ(1, starts);
    ASSERT_EQ(1, stops);
    /* D2 = (3 << 6) & 0xff00 | boolean 0 */
    ASSERT_EQ(0x0000, r);
    /* D5w = mount_proc (4) with its low byte replaced by (state == 3) = 0 */
    ASSERT_EQ(0x0000, avail);
    ASSERT_EQ(DISK_MOUNT_ASSIGNED, DISK_VOL(3)->mount_state);
}

TEST(busy_pv_is_accepted)
{
    uint16_t v = 3, lv = 1;
    int32_t avail = 0;
    status_$t st = 0;
    reset();
    DISK_VOL(3)->mount_state = DISK_MOUNT_BUSY;
    DISK_VOL(3)->mount_proc = 4;
    DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(DISK_MOUNT_BUSY, DISK_VOL(3)->mount_state);
}

TEST(logical_volume_rejected)
{
    uint16_t v = 3, lv = 1;
    int32_t avail = 0;
    status_$t st = 0;
    reset();
    DISK_VOL(3)->lv_start = 0x100;
    DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(status_$operation_requires_a_physical_volume, st);
}

TEST(label_read_failure_restores_state)
{
    uint16_t v = 3, lv = 1;
    int32_t avail = 0;
    status_$t st = 0;
    reset();
    get_block_status = 0x12345;
    DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(0x12345, st);
    ASSERT_EQ(3, get_block_vol);
    ASSERT_EQ(0, get_block_daddr);
    ASSERT_EQ((unsigned long)&PV_LABEL_$UID, (unsigned long)get_block_uid);
    ASSERT_EQ(0, get_block_flags);
    ASSERT_EQ(0, set_buffs);
    ASSERT_EQ(DISK_MOUNT_ASSIGNED, DISK_VOL(3)->mount_state);
}

/* LV1: size from LV2's start (0x1000 - 0x100); no end entry -> avail 0;
 * lands in the lowest free slot (1). */
TEST(assigns_lowest_free_slot)
{
    uint16_t v = 3, lv = 1;
    int32_t avail = -1;
    status_$t st = 0;
    uint16_t r;
    reset();
    r = DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, r);
    ASSERT_EQ(0, avail);
    ASSERT_EQ(1, set_buffs);
    ASSERT_EQ(0x0c, set_buff_flags);
    ASSERT_EQ((unsigned long)&set_buff_cell != 0, set_buff_cell != NULL);
    ASSERT_EQ(0x100, DISK_VOL(1)->lv_start);
    ASSERT_EQ(0x1000 - 0x100, DISK_VOL(1)->addr_start);
    ASSERT_EQ(0x6000, DISK_VOL(1)->addr_end);       /* copied from the PV */
    ASSERT_EQ(5, DISK_VOL(1)->num_heads);
    ASSERT_EQ(9, DISK_VOL(1)->mount_proc);
    ASSERT_EQ(0, DISK_VOL(1)->as_options);
    ASSERT_EQ(DISK_MOUNT_ASSIGNED, DISK_VOL(1)->mount_state);
    ASSERT_EQ(DISK_MOUNT_ASSIGNED, DISK_VOL(3)->mount_state);
}

/* LV2: no LV3 -> size from addr_start; end entry present -> avail. */
TEST(size_from_pv_end_and_blocks_avail)
{
    uint16_t v = 3, lv = 2;
    int32_t avail = -1;
    status_$t st = 0;
    uint16_t r;
    reset();
    DISK_VOL(3)->as_options = 0x55;
    r = DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, r);
    ASSERT_EQ(0x3000 - 0x1000, avail);
    ASSERT_EQ(0x5000 - 0x1000, DISK_VOL(1)->addr_start);
}

TEST(lv_start_beyond_pv_rejected)
{
    uint16_t v = 3, lv = 1;
    int32_t avail = 0;
    status_$t st = 0;
    uint16_t r;
    reset();
    *(uint32_t *)(label + 0x38 + 1 * 4) = 0x5001;
    r = DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(status_$invalid_logical_volume_index, st);
    ASSERT_EQ(1, set_buffs);                /* released before the check */
    ASSERT_EQ(0x5001, r);                   /* low word of the LV start */
}

TEST(already_assigned_is_in_use)
{
    uint16_t v = 3, lv = 1;
    int32_t avail = 0;
    status_$t st = 0;
    reset();
    DISK_VOL(5)->mount_state = DISK_MOUNT_ASSIGNED;
    DISK_VOL(5)->dev_info = &dev_a;
    DISK_VOL(5)->dev_unit = 2;
    DISK_VOL(5)->lv_start = 0x100;
    DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(status_$volume_in_use, st);
}

TEST(table_full)
{
    uint16_t v = 3, lv = 1;
    int32_t avail = 0;
    status_$t st = 0;
    int i;
    reset();
    for (i = 1; i <= 6; i++) {
        if (i != 3) {
            DISK_VOL(i)->mount_state = DISK_MOUNT_MOUNTED;
            DISK_VOL(i)->dev_unit = 7;      /* different drive */
        }
    }
    DISK_$LV_ASSIGN(&v, &lv, &avail, &st);
    ASSERT_EQ(status_$volume_table_full, st);
}

int main(void)
{
    printf("test_lv_assign:\n");
    RUN_TEST(invalid_volume_index_no_lock);
    RUN_TEST(invalid_lv_index);
    RUN_TEST(not_assigned_to_caller);
    RUN_TEST(busy_pv_is_accepted);
    RUN_TEST(logical_volume_rejected);
    RUN_TEST(label_read_failure_restores_state);
    RUN_TEST(assigns_lowest_free_slot);
    RUN_TEST(size_from_pv_end_and_blocks_avail);
    RUN_TEST(lv_start_beyond_pv_rejected);
    RUN_TEST(already_assigned_is_in_use);
    RUN_TEST(table_full);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
