/*
 * volx/test/test_mount.c - VOLX_$MOUNT (0x00E6B118) status reporting and
 * mount-parameter packing.
 *
 * Covered (source-rurk):
 *   - a VTOC_$MOUNT failure other than status_$disk_write_protected takes
 *     0x00E6B220 -> 0x00E6B30C: DISK_$DISMOUNT of the logical volume, then
 *     the common tail at 0x00E6B32C, where local_status (A6-0x2C) is still
 *     zero, so the caller is handed VTOC_$MOUNT's own status (A6-0x24) - not
 *     a bare zero
 *   - VTOC_$DISMOUNT at 0x00E6B2FA reports into its own cell (A6-0x28), so a
 *     later failure path must not lose vtoc_status
 *   - the packed mount-parameter word (0x00E6B1C6-0x00E6B204) applies no mask
 *     to bus or lv_num
 *   - the write-protect arm (0x00E6B224-0x00E6B234)
 *   - "already mounted" (0x8001E) suppresses the physical dismount
 */

#include <stdio.h>
#include <string.h>

#include "volx/volx_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-46s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Mocked globals
 * ============================================================================ */

uid_t UID_$NIL = { 0, 0 };

/* ============================================================================
 * Mock bookkeeping
 * ============================================================================ */

static status_$t mock_pv_mount_status;
static int16_t   mock_pv_mount_result;

static status_$t mock_lv_uid_status;
static status_$t mock_lv_mount_status;
static int16_t   mock_lv_mount_result;

static int       mock_vtoc_mount_calls;
static uint16_t  mock_vtoc_mount_param;
static int8_t    mock_vtoc_mount_salvage;
static int8_t    mock_vtoc_mount_wprot;
static status_$t mock_vtoc_mount_status;

static int       mock_get_name_dirs_calls;
static status_$t mock_get_name_dirs_status;

static int       mock_add_mount_calls;
static status_$t mock_add_mount_status;

static int       mock_vtoc_dismount_calls;
static int       mock_disk_dismount_calls;
static int16_t   mock_disk_dismount_idx[4];

/* ============================================================================
 * Mocks
 * ============================================================================ */

int16_t DISK_$PV_MOUNT(int16_t dev, int16_t bus, int16_t ctlr, status_$t *status)
{
    (void)dev; (void)bus; (void)ctlr;
    *status = mock_pv_mount_status;
    return mock_pv_mount_result;
}

void DISK_$LV_UID(int16_t pv_idx, int16_t lv_num, uid_t *lv_uid, status_$t *status)
{
    (void)pv_idx; (void)lv_num;
    lv_uid->high = 0xAAAA0000u;
    lv_uid->low  = 0xBBBB0000u;
    *status = mock_lv_uid_status;
}

int16_t DISK_$LV_MOUNT(uid_t *lv_uid, status_$t *status)
{
    (void)lv_uid;
    *status = mock_lv_mount_status;
    return mock_lv_mount_result;
}

void DISK_$DISMOUNT(uint16_t idx)
{
    if (mock_disk_dismount_calls < 4) {
        mock_disk_dismount_idx[mock_disk_dismount_calls] = (int16_t)idx;
    }
    mock_disk_dismount_calls++;
}

void VTOC_$MOUNT(int16_t vol_idx, uint16_t mount_param, uint8_t salvage_ok,
                 char write_prot, status_$t *status)
{
    (void)vol_idx;
    mock_vtoc_mount_calls++;
    mock_vtoc_mount_param   = mount_param;
    mock_vtoc_mount_salvage = (int8_t)salvage_ok;
    mock_vtoc_mount_wprot   = (int8_t)write_prot;
    *status = mock_vtoc_mount_status;
}

void VTOC_$DISMOUNT(uint16_t vol_idx, uint8_t flags, status_$t *status)
{
    (void)vol_idx; (void)flags;
    mock_vtoc_dismount_calls++;
    /* A real dismount reports its own status; if VOLX_$MOUNT were to hand it
     * the vtoc_status cell this would clobber the failure being reported. */
    *status = 0x00DEAD01;
}

void VTOC_$GET_NAME_DIRS(int16_t vol_idx, uid_t *name_dir, uid_t *root_dir,
                         status_$t *status)
{
    (void)vol_idx;
    mock_get_name_dirs_calls++;
    name_dir->high = 0x11110000u; name_dir->low = 0x22220000u;
    root_dir->high = 0x33330000u; root_dir->low = 0x44440000u;
    *status = mock_get_name_dirs_status;
}

void DIR_$ADD_MOUNT(uid_t *parent, uid_t *dir, status_$t *status)
{
    (void)parent; (void)dir;
    mock_add_mount_calls++;
    *status = mock_add_mount_status;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../volx_data.c"
#include "../mount.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

static int16_t in_dev, in_bus, in_ctlr, in_lv;
static int8_t  in_salvage, in_wprot;
static uid_t   in_parent;

static void reset_mocks(void)
{
    memset(VOLX_$TABLE, 0, sizeof(volx_$entry_t) * VOLX_MAX_VOLUMES);

    mock_pv_mount_status = status_$ok;
    mock_pv_mount_result = 3;
    mock_lv_uid_status   = status_$ok;
    mock_lv_mount_status = status_$ok;
    mock_lv_mount_result = 2;

    mock_vtoc_mount_calls  = 0;
    mock_vtoc_mount_param  = 0;
    mock_vtoc_mount_status = status_$ok;

    mock_get_name_dirs_calls  = 0;
    mock_get_name_dirs_status = status_$ok;

    mock_add_mount_calls  = 0;
    mock_add_mount_status = status_$ok;

    mock_vtoc_dismount_calls = 0;
    mock_disk_dismount_calls = 0;
    memset(mock_disk_dismount_idx, 0, sizeof(mock_disk_dismount_idx));

    in_dev = 1; in_bus = 2; in_ctlr = 3; in_lv = 4;
    in_salvage = 0; in_wprot = 0;
    in_parent = UID_$NIL;
}

static void call_mount(uid_t *dir_out, status_$t *status)
{
    VOLX_$MOUNT(&in_dev, &in_bus, &in_ctlr, &in_lv,
                &in_salvage, &in_wprot, &in_parent, dir_out, status);
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/*
 * 0x00E6B21A `cmpi.l #0x80007` / `bne.w 0x00E6B30C`.  The failure must reach
 * the caller: the tail at 0x00E6B32C sees local_status == 0 and copies
 * vtoc_status.
 */
TEST(vtoc_mount_failure_is_reported)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0x5A5A5A5A;

    reset_mocks();
    mock_vtoc_mount_status = 0x00140009;
    call_mount(&dir_out, &status);

    ASSERT_EQ(0x00140009, status);
    /* 0x00E6B30C dismounts the logical volume ... */
    ASSERT_EQ(2, mock_disk_dismount_calls);
    ASSERT_EQ(2, mock_disk_dismount_idx[0]);   /* vol_idx */
    /* ... then 0x00E6B318 dismounts the physical volume */
    ASSERT_EQ(3, mock_disk_dismount_idx[1]);   /* pv_idx */
    /* the VTOC was never mounted, so it is not dismounted */
    ASSERT_EQ(0, mock_vtoc_dismount_calls);
    /* and the run stops before VTOC_$GET_NAME_DIRS */
    ASSERT_EQ(0, mock_get_name_dirs_calls);
}

/*
 * 0x00E6B2FA VTOC_$DISMOUNT writes A6-0x28, a cell nobody reads.  A
 * GET_NAME_DIRS failure must therefore still report local_status, and a
 * dismount that reported into vtoc_status would be visible here.
 */
TEST(vtoc_dismount_status_is_its_own_cell)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0;

    reset_mocks();
    mock_get_name_dirs_status = 0x00140003;
    call_mount(&dir_out, &status);

    ASSERT_EQ(1, mock_vtoc_dismount_calls);
    ASSERT_EQ(0x00140003, status);   /* not the mock's 0x00DEAD01 */
    ASSERT_EQ(1, mock_disk_dismount_calls);
    ASSERT_EQ(3, mock_disk_dismount_idx[0]);
}

/* 0x00E6B224-0x00E6B22C: a write-protected mount the caller asked for. */
TEST(write_protect_requested_is_success)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0x1234;

    reset_mocks();
    in_wprot = -1;                            /* Domain boolean true */
    mock_vtoc_mount_status = status_$disk_write_protected;
    call_mount(&dir_out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, mock_disk_dismount_calls);
    ASSERT_EQ(1, mock_get_name_dirs_calls);
}

/* 0x00E6B22E: not requested - the caller gets the 0xFFFF warning subcode. */
TEST(write_protect_unrequested_is_warning)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0;

    reset_mocks();
    in_wprot = 0;
    mock_vtoc_mount_status = status_$disk_write_protected;
    call_mount(&dir_out, &status);

    ASSERT_EQ(status_$volume_disk_is_write_protected, status);
    ASSERT_EQ(0x0014FFFF, status);
    ASSERT_EQ(0, mock_disk_dismount_calls);
}

/*
 * 0x00E6B1C6-0x00E6B204: high byte = (dev << 3) | bus, low byte =
 * (ctlr << 4) | lv_num, with NO mask on bus or lv_num.
 */
TEST(mount_param_packing)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0;

    reset_mocks();
    in_dev = 1; in_bus = 2; in_ctlr = 3; in_lv = 4;
    call_mount(&dir_out, &status);
    ASSERT_EQ(1, mock_vtoc_mount_calls);
    ASSERT_EQ(0x0A34, mock_vtoc_mount_param);   /* (1<<3)|2 = 0x0A, (3<<4)|4 = 0x34 */

    /* dev keeps only the five bits that survive `lsl.b #3` */
    reset_mocks();
    in_dev = 0x1F; in_bus = 0; in_ctlr = 0x0F; in_lv = 0;
    call_mount(&dir_out, &status);
    ASSERT_EQ(0xF8F0, mock_vtoc_mount_param);
}

/* An unmasked bus spills into the dev field - the image has no `andi #7`. */
TEST(mount_param_bus_and_lv_are_unmasked)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0;

    reset_mocks();
    in_dev = 0; in_bus = 0x0F; in_ctlr = 0; in_lv = 0xFF;
    call_mount(&dir_out, &status);

    /* &7 on bus would give 0x07; &0xF on lv_num would give 0x0F */
    ASSERT_EQ(0x0FFF, mock_vtoc_mount_param);
}

/* 0x00E6B176-0x00E6B18C / 0x00E6B318: 0x8001E is not an error, and it also
 * suppresses the physical dismount on the way out. */
TEST(already_mounted_suppresses_pv_dismount)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0;

    reset_mocks();
    mock_pv_mount_status = status_$disk_already_mounted;
    mock_lv_uid_status   = 0x00140003;   /* force the dismount_pv path */
    call_mount(&dir_out, &status);

    ASSERT_EQ(0x00140003, status);
    ASSERT_EQ(0, mock_disk_dismount_calls);
}

/* Any other DISK_$PV_MOUNT error returns at once, with no dismounts. */
TEST(pv_mount_failure_returns_immediately)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0;

    reset_mocks();
    mock_pv_mount_status = 0x00140005;
    call_mount(&dir_out, &status);

    ASSERT_EQ(0x00140005, status);
    ASSERT_EQ(0, mock_disk_dismount_calls);
    ASSERT_EQ(0, mock_vtoc_mount_calls);
}

/* 0x00E6B2BC-0x00E6B2F6: the success path fills the 1-based table entry. */
TEST(success_fills_the_table_entry)
{
    uid_t dir_out = { 0, 0 };
    status_$t status = 0x1111;
    volx_$entry_t *e;

    reset_mocks();
    mock_lv_mount_result = 2;
    call_mount(&dir_out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x33330000u, dir_out.high);
    ASSERT_EQ(0x44440000u, dir_out.low);

    e = VOLX_$ENTRY(2);
    ASSERT_EQ(0x33330000u, e->dir_uid.high);
    ASSERT_EQ(0x44440000u, e->dir_uid.low);
    ASSERT_EQ(0xAAAA0000u, e->lv_uid.high);
    ASSERT_EQ(0xBBBB0000u, e->lv_uid.low);
    ASSERT_EQ(1, e->dev);
    ASSERT_EQ(2, e->bus);
    ASSERT_EQ(3, e->ctlr);
    ASSERT_EQ(4, e->lv_num);
    /* the mount point was UID_$NIL, so DIR_$ADD_MOUNT is skipped */
    ASSERT_EQ(0, mock_add_mount_calls);
}

int main(void)
{
    printf("VOLX_$MOUNT tests\n");
    RUN_TEST(vtoc_mount_failure_is_reported);
    RUN_TEST(vtoc_dismount_status_is_its_own_cell);
    RUN_TEST(write_protect_requested_is_success);
    RUN_TEST(write_protect_unrequested_is_warning);
    RUN_TEST(mount_param_packing);
    RUN_TEST(mount_param_bus_and_lv_are_unmasked);
    RUN_TEST(already_mounted_suppresses_pv_dismount);
    RUN_TEST(pv_mount_failure_returns_immediately);
    RUN_TEST(success_fills_the_table_entry);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
