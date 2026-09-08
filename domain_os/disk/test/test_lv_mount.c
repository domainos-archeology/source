/*
 * disk/test/test_lv_mount.c - unit tests for DISK_$LV_MOUNT (0x00E6CA3A)
 *
 * The real disk/lv_mount.c is #included below and driven against a mock
 * volume table and mock label blocks, so the slot scan, the PV scan, the
 * label field reads (bat_$label_t, bead source-f5j9) and the descriptor copy
 * all run through the real function.
 *
 * Covered:
 *   - an LV that is already mounted returns its own slot and
 *     status_$disk_already_mounted (0x00E6CAA0)
 *   - a full LV table returns status_$volume_table_full (0x00E6CABA)
 *   - a successful mount copies the whole 0x48-byte PV descriptor
 *     (0x00E6CBBE), then rewrites lv_start (0x00E6CBC4), addr_start as
 *     first_data_block + total_blocks (0x00E6CBC8), the UID (0x00E6CBD8),
 *     as_options (0x00E6CBE0), mount_state (0x00E6CBE4) and bat_step in both
 *     the LV and the PV descriptor (0x00E6CBEA, 0x00E6CBF4)
 *   - a label whose version is above 1 is skipped (0x00E6CB80)
 *   - no match anywhere returns status_$logical_volume_not_found (0x00E6CC3A)
 */

#include "disk/disk_internal.h"

#include "bat/bat.h"

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

ml_$exclusion_t MOUNT_LOCK;
ml_$exclusion_t ml_$exclusion_t_00e7a274;

uid_t PV_LABEL_$UID;
uid_t LV_LABEL_$UID;

static int lock_starts;
static int lock_stops;

void ML_$EXCLUSION_START(ml_$exclusion_t *excl) { (void)excl; lock_starts++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)  { (void)excl; lock_stops++; }

/*
 * Mock block cache.  The PV label carries the ten-entry LV block table at
 * +0x3c; each LV block number names one of the mock LV labels below.
 */
#define MOCK_LV_SLOTS 2

static uint8_t mock_pv_label[0x400];
static bat_$label_t mock_lv_label[MOCK_LV_SLOTS];
static uint32_t mock_lv_block[MOCK_LV_SLOTS];

static status_$t get_block_status;
static int set_buff_calls;
static uint16_t last_set_buff_flags;

void *DISK_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *expected_uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    int i;

    (void)vol_idx; (void)block_hint; (void)block_type; (void)flags;
    (void)expected_uid;
    *status = get_block_status;
    if (get_block_status != status_$ok) {
        return NULL;
    }
    if (daddr == 0) {
        return mock_pv_label;
    }
    for (i = 0; i < MOCK_LV_SLOTS; i++) {
        if (mock_lv_block[i] == (uint32_t)daddr) {
            return &mock_lv_label[i];
        }
    }
    return NULL;
}

void DISK_$SET_BUFF(void *buffer, uint16_t flags, void *param_3)
{
    (void)buffer;
    set_buff_calls++;
    last_set_buff_flags = flags;
    /* The original hands SET_BUFF a scratch status cell it never reads. */
    *(status_$t *)param_3 = 0x0BADF00D;
}

#include "../lv_mount.c"

/* DISK_$DIAG, DISK_$DO_CHKSUM and the module exclusion lock are cells of the
 * DISK_ module block (disk/disk.h), so the host build provides the block. */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/* ================================================================
 * Fixtures
 * ================================================================ */
#define TARGET_UID_HIGH 0x11112222u
#define TARGET_UID_LOW  0x33334444u

static uid_t target_uid;
static status_$t status;

static void reset_fixture(void)
{
    int i;

    memset(mock_disk_data, 0, sizeof(mock_disk_data));
    memset(mock_pv_label, 0, sizeof(mock_pv_label));
    memset(mock_lv_label, 0, sizeof(mock_lv_label));
    memset(mock_lv_block, 0, sizeof(mock_lv_block));

    lock_starts = 0;
    lock_stops = 0;
    set_buff_calls = 0;
    get_block_status = status_$ok;
    status = 0x0BADF00D;

    target_uid.high = TARGET_UID_HIGH;
    target_uid.low = TARGET_UID_LOW;

    /* Volumes 1..6 are the LV slots; leave them all free. */
    for (i = 1; i <= 10; i++) {
        DISK_VOL(i)->mount_state = DISK_MOUNT_FREE;
    }
}

/* Make volume `idx` a mounted physical volume carrying one LV. */
static void make_pv(int idx, uint32_t lv_block, int lv_slot)
{
    disk_$volume_t *v = DISK_VOL(idx);

    v->mount_state = DISK_MOUNT_MOUNTED;
    v->lv_start = 0;
    v->dev_unit = (uint16_t)(0x80 + idx);   /* a witness for the block copy */
    v->bat_step = 0;

    mock_lv_block[lv_slot] = lv_block;
    *(uint32_t *)(mock_pv_label + 0x3c + lv_slot * 4) = lv_block;
}

static bat_$label_t *make_lv_label(int lv_slot, int16_t version,
                                   uint32_t uid_high, uint32_t uid_low)
{
    bat_$label_t *l = &mock_lv_label[lv_slot];

    l->version = version;
    l->lv_uid.high = uid_high;
    l->lv_uid.low = uid_low;
    l->total_blocks = 0x1000;
    l->first_data_block = 0x40;
    l->bat_step = 3;
    return l;
}

/* ================================================================
 * Tests
 * ================================================================ */

/* 0x00E6CA96-0x00E6CAA8: the LV is already in the table. */
static int test_already_mounted(void)
{
    int16_t slot;

    reset_fixture();
    DISK_VOL(4)->mount_state = DISK_MOUNT_MOUNTED;
    DISK_VOL(4)->lv_uid.high = TARGET_UID_HIGH;
    DISK_VOL(4)->lv_uid.low = TARGET_UID_LOW;

    slot = DISK_$LV_MOUNT(&target_uid, &status);

    CHECK_EQ(4, slot);
    CHECK_EQ(status_$disk_already_mounted, status);
    CHECK_EQ(1, lock_starts);
    CHECK_EQ(1, lock_stops);
    return 0;
}

/* 0x00E6CAB6-0x00E6CAC2: every LV slot is in use and none of them matches. */
static int test_table_full(void)
{
    int i;
    int16_t slot;

    reset_fixture();
    for (i = 1; i <= 6; i++) {
        DISK_VOL(i)->mount_state = DISK_MOUNT_MOUNTED;
        DISK_VOL(i)->lv_uid.high = 0xDEADBEEFu;
        DISK_VOL(i)->lv_uid.low = (uint32_t)i;
    }

    slot = DISK_$LV_MOUNT(&target_uid, &status);

    CHECK_EQ(0, slot);
    CHECK_EQ(status_$volume_table_full, status);
    return 0;
}

/* 0x00E6CC3A: no PV carries the wanted LV. */
static int test_not_found(void)
{
    int16_t slot;

    reset_fixture();
    make_pv(8, 5, 0);
    make_lv_label(0, 1, 0x55556666u, 0x77778888u);

    slot = DISK_$LV_MOUNT(&target_uid, &status);

    CHECK_EQ(1, slot);                       /* lowest free LV slot */
    CHECK_EQ(status_$logical_volume_not_found, status);
    /* one PV label release plus one LV label release */
    CHECK_EQ(2, set_buff_calls);
    return 0;
}

/* 0x00E6CB80: a label whose version is above 1 is not a candidate. */
static int test_version_too_new(void)
{
    int16_t slot;

    reset_fixture();
    make_pv(8, 5, 0);
    make_lv_label(0, 2, TARGET_UID_HIGH, TARGET_UID_LOW);

    slot = DISK_$LV_MOUNT(&target_uid, &status);

    CHECK_EQ(status_$logical_volume_not_found, status);
    CHECK_EQ(1, slot);
    return 0;
}

/* 0x00E6CBBE-0x00E6CBF4: the whole successful path. */
static int test_mount_success(void)
{
    int16_t slot;
    disk_$volume_t *lv;
    disk_$volume_t *pv;

    reset_fixture();
    make_pv(8, 5, 0);
    make_lv_label(0, 1, TARGET_UID_HIGH, TARGET_UID_LOW);

    /* Occupy slots 6..2 so that only slot 1 is free. */
    {
        int i;
        for (i = 2; i <= 6; i++) {
            DISK_VOL(i)->mount_state = DISK_MOUNT_MOUNTED;
            DISK_VOL(i)->lv_uid.high = 0xDEADBEEFu;
            DISK_VOL(i)->lv_uid.low = (uint32_t)i;
        }
    }

    slot = DISK_$LV_MOUNT(&target_uid, &status);

    CHECK_EQ(1, slot);
    CHECK_EQ(status_$ok, status);

    lv = DISK_VOL(1);
    pv = DISK_VOL(8);

    /* the 0x48-byte record really was copied from the PV descriptor */
    CHECK_EQ(0x88, lv->dev_unit);
    /* 0x00E6CBC4 */
    CHECK_EQ(5, lv->lv_start);
    /* 0x00E6CBC8-0x00E6CBD0: first_data_block + total_blocks */
    CHECK_EQ(0x40 + 0x1000, lv->addr_start);
    /* 0x00E6CBD8-0x00E6CBDC */
    CHECK_EQ(TARGET_UID_HIGH, lv->lv_uid.high);
    CHECK_EQ(TARGET_UID_LOW, lv->lv_uid.low);
    /* 0x00E6CBE0, 0x00E6CBE4 */
    CHECK_EQ(0, lv->as_options);
    CHECK_EQ(DISK_MOUNT_MOUNTED, lv->mount_state);
    /* 0x00E6CBEA, 0x00E6CBF4 */
    CHECK_EQ(3, lv->bat_step);
    CHECK_EQ(3, pv->bat_step);

    CHECK_EQ(1, lock_starts);
    CHECK_EQ(1, lock_stops);
    return 0;
}

/*
 * The label offsets DISK_$LV_MOUNT depends on, cross-checked against the
 * BAT manager's copy loop (0x00E3B7DA) that reads the same eight longwords.
 */
static int test_label_offsets(void)
{
    CHECK_EQ(0x00, __builtin_offsetof(bat_$label_t, version));
    CHECK_EQ(0x24, __builtin_offsetof(bat_$label_t, lv_uid));
    CHECK_EQ(0x2C, __builtin_offsetof(bat_$label_t, total_blocks));
    CHECK_EQ(0x38, __builtin_offsetof(bat_$label_t, first_data_block));
    CHECK_EQ(0x40, __builtin_offsetof(bat_$label_t, bat_step));
    return 0;
}

int main(void)
{
    printf("DISK_$LV_MOUNT tests\n");

    RUN_TEST(already_mounted);
    RUN_TEST(table_full);
    RUN_TEST(not_found);
    RUN_TEST(version_too_new);
    RUN_TEST(mount_success);
    RUN_TEST(label_offsets);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
