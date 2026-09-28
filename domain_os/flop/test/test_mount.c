/*
 * flop/test/test_mount.c - flop_$mount_floppy (0x00E323E6)
 *
 * Drives every path of the routine through mocks of VOLX_$MOUNT /
 * VOLX_$DISMOUNT, NAME_$GET_NODE_UID, NAME_$RESOLVE and the three DIR
 * calls, and checks which constant cell each argument is (the sharing of
 * 0x00E32540 as dev AND lv_num, and of 0x00E3253E as bus AND ctlr).
 */

#include <stdio.h>
#include <string.h>

#include "flop/flop_internal.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-48s ", #name); \
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

#define ASSERT_PTR_EQ(expected, actual) do { \
    const void *_e = (const void *)(expected); \
    const void *_a = (const void *)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %p, Got: %p at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_STR_EQ(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        printf("FAILED\n    Expected: \"%s\", Got: \"%s\" at line %d\n", \
               (expected), (actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Globals the code under test references
 * ========================================================================== */

uid_t UID_$NIL = { 0, 0 };

/* ==========================================================================
 * Mocks
 * ========================================================================== */

static const uid_t mock_volume_uid = { 0xF10BF10Bu, 0x00000001u };
static const uid_t mock_node_uid   = { 0x0000A0DEu, 0x00000002u };

static status_$t mock_mount_status;
static status_$t mock_addu_status;
static status_$t mock_resolve_status;
static uid_t     mock_resolve_uid;
static status_$t mock_set_dad_status;

static int       mock_mount_calls;
static int16_t  *mock_mount_dev, *mock_mount_bus, *mock_mount_ctlr, *mock_mount_lv;
static int8_t   *mock_mount_salvage, *mock_mount_wprot;
static uid_t    *mock_mount_parent;

static int       mock_dismount_calls;
static int16_t  *mock_dismount_dev, *mock_dismount_bus, *mock_dismount_ctlr,
                *mock_dismount_lv;
static int8_t   *mock_dismount_force;
static uid_t     mock_dismount_uid;

static int       mock_addu_calls;
static uid_t     mock_addu_dir, mock_addu_entry;
static char     *mock_addu_name;
static int16_t  *mock_addu_len;

static int       mock_resolve_calls;
static char     *mock_resolve_path;
static int16_t  *mock_resolve_len;

static int       mock_set_dad_calls;
static uid_t     mock_set_dad_dir, mock_set_dad_parent;

static int       mock_dropu_calls;
static uid_t     mock_dropu_dir, mock_dropu_entry;
static char     *mock_dropu_name;
static uint16_t *mock_dropu_len;

void VOLX_$MOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                 int8_t *salvage_ok, int8_t *write_prot, uid_t *parent_uid,
                 uid_t *dir_uid_ret, status_$t *status)
{
    mock_mount_calls++;
    mock_mount_dev = dev;
    mock_mount_bus = bus;
    mock_mount_ctlr = ctlr;
    mock_mount_lv = lv_num;
    mock_mount_salvage = salvage_ok;
    mock_mount_wprot = write_prot;
    mock_mount_parent = parent_uid;
    *dir_uid_ret = mock_volume_uid;
    *status = mock_mount_status;
}

void VOLX_$DISMOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                    uid_t *entry_uid, int8_t *force, status_$t *status)
{
    mock_dismount_calls++;
    mock_dismount_dev = dev;
    mock_dismount_bus = bus;
    mock_dismount_ctlr = ctlr;
    mock_dismount_lv = lv_num;
    mock_dismount_uid = *entry_uid;
    mock_dismount_force = force;
    *status = 0x00777777;       /* must never surface */
}

void NAME_$GET_NODE_UID(uid_t *node_uid)
{
    *node_uid = mock_node_uid;
}

void DIR_$ADDU(uid_t *dir_uid, char *name, int16_t *name_len, uid_t *entry_uid,
               status_$t *status_ret)
{
    mock_addu_calls++;
    mock_addu_dir = *dir_uid;
    mock_addu_name = name;
    mock_addu_len = name_len;
    mock_addu_entry = *entry_uid;
    *status_ret = mock_addu_status;
}

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid,
                   status_$t *status_ret)
{
    mock_resolve_calls++;
    mock_resolve_path = path;
    mock_resolve_len = path_len;
    *resolved_uid = mock_resolve_uid;
    *status_ret = mock_resolve_status;
}

void DIR_$SET_DAD(uid_t *dir_uid, uid_t *parent_uid, status_$t *status_ret)
{
    mock_set_dad_calls++;
    mock_set_dad_dir = *dir_uid;
    mock_set_dad_parent = *parent_uid;
    *status_ret = mock_set_dad_status;
}

void DIR_$DROPU(uid_t *dir_uid, char *name, uint16_t *name_len,
                uid_t *entry_uid, status_$t *status_ret)
{
    mock_dropu_calls++;
    mock_dropu_dir = *dir_uid;
    mock_dropu_name = name;
    mock_dropu_len = name_len;
    mock_dropu_entry = *entry_uid;
    *status_ret = 0x00666666;   /* must never surface */
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../mount.c"

static void reset(void)
{
    mock_mount_status = status_$ok;
    mock_addu_status = status_$ok;
    mock_resolve_status = status_$ok;
    mock_resolve_uid = mock_volume_uid;
    mock_set_dad_status = status_$ok;

    mock_mount_calls = 0;
    mock_dismount_calls = 0;
    mock_addu_calls = 0;
    mock_resolve_calls = 0;
    mock_set_dad_calls = 0;
    mock_dropu_calls = 0;

    flop_zero_byte = 0x00;
    flop_word_four = 0x0004;
    flop_flp_name_len = 0x0003;
    flop_zero_word = 0x0000;
    flop_word_one = 0x0001;
    flop_ff_byte = 0xFF;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* gsk read 0xe32538: 00 00 00 04 00 03 00 00 00 01 ff 00 "flp" NUL "/flp" */
TEST(constant_cells)
{
    reset();
    ASSERT_EQ(0x00, flop_zero_byte);        /* 0x00E32538 */
    ASSERT_EQ(0x0004, flop_word_four);      /* 0x00E3253A */
    ASSERT_EQ(0x0003, flop_flp_name_len);   /* 0x00E3253C */
    ASSERT_EQ(0x0000, flop_zero_word);      /* 0x00E3253E */
    ASSERT_EQ(0x0001, flop_word_one);       /* 0x00E32540 */
    ASSERT_EQ(0xFF, flop_ff_byte);          /* 0x00E32542 */
    ASSERT_STR_EQ("flp", flop_flp_name);    /* 0x00E32544 */
    ASSERT_STR_EQ("/flp", flop_flp_path);   /* 0x00E32548 */
}

/* 0x00E323F2-0x00E32416: which cell each VOLX_$MOUNT argument is. */
TEST(mount_arguments)
{
    status_$t status = 0x55555555;

    reset();
    flop_$mount_floppy(&status);

    ASSERT_EQ(1, mock_mount_calls);
    ASSERT_PTR_EQ(&flop_word_one, mock_mount_dev);      /* pea (0x12c,PC) */
    ASSERT_PTR_EQ(&flop_zero_word, mock_mount_bus);     /* move.l (SP),-(SP) */
    ASSERT_PTR_EQ(&flop_zero_word, mock_mount_ctlr);    /* pea (0x130,PC) */
    ASSERT_PTR_EQ(&flop_word_one, mock_mount_lv);       /* pea (0x136,PC) */
    ASSERT_PTR_EQ(&flop_ff_byte, mock_mount_salvage);   /* pea (0x13c,PC) */
    ASSERT_PTR_EQ(&flop_zero_byte, mock_mount_wprot);   /* pea (0x136,PC) */
    ASSERT_PTR_EQ(&UID_$NIL, mock_mount_parent);        /* move.l #0xe1737c */
    ASSERT_EQ(1, *mock_mount_dev);
    ASSERT_EQ(0, *mock_mount_bus);
    ASSERT_EQ(-1, *mock_mount_salvage);
    ASSERT_EQ(0, *mock_mount_wprot);
}

/* The all-clear path: ADDU, SET_DAD, and mount_status (0) comes back. */
TEST(success_path)
{
    status_$t status = 0x55555555;

    reset();
    flop_$mount_floppy(&status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_addu_calls);
    ASSERT_EQ(mock_node_uid.high, mock_addu_dir.high);
    ASSERT_EQ(mock_node_uid.low, mock_addu_dir.low);
    ASSERT_PTR_EQ(flop_flp_name, mock_addu_name);
    ASSERT_PTR_EQ(&flop_flp_name_len, mock_addu_len);
    ASSERT_EQ(mock_volume_uid.high, mock_addu_entry.high);
    ASSERT_EQ(mock_volume_uid.low, mock_addu_entry.low);
    ASSERT_EQ(0, mock_resolve_calls);
    ASSERT_EQ(1, mock_set_dad_calls);
    ASSERT_EQ(mock_volume_uid.low, mock_set_dad_dir.low);
    ASSERT_EQ(mock_node_uid.low, mock_set_dad_parent.low);
    ASSERT_EQ(0, mock_dismount_calls);
    ASSERT_EQ(0, mock_dropu_calls);
}

/* 0x00E32420-0x00E3243A: a real mount error, bit 31 stripped, is returned
 * before anything else happens. */
TEST(mount_error_returns_immediately)
{
    status_$t status = 0x55555555;

    reset();
    mock_mount_status = (status_$t)0x800B0004;
    flop_$mount_floppy(&status);

    ASSERT_EQ(0x000B0004, status);
    ASSERT_EQ(0, mock_addu_calls);
    ASSERT_EQ(0, mock_dismount_calls);
}

/* 0x00E32430: the write-protected warning (with or without bit 31) is not
 * an error; the stripped value is what the success path returns. */
TEST(write_protected_warning_is_forgiven_and_returned)
{
    status_$t status = 0x55555555;

    reset();
    mock_mount_status = (status_$t)0x8014FFFF;
    flop_$mount_floppy(&status);

    ASSERT_EQ(status_$volume_disk_is_write_protected, status);
    ASSERT_EQ(1, mock_addu_calls);
    ASSERT_EQ(1, mock_set_dad_calls);
    ASSERT_EQ(0, mock_dismount_calls);
}

/* 0x00E3247A-0x00E324B8: "flp" already exists and resolves to this very
 * volume - the 0xE0003 is forgiven and SET_DAD still runs. */
TEST(existing_link_to_same_volume_is_ok)
{
    status_$t status = 0x55555555;

    reset();
    mock_addu_status = status_$name_already_exists;
    flop_$mount_floppy(&status);

    ASSERT_EQ(1, mock_resolve_calls);
    ASSERT_PTR_EQ(flop_flp_path, mock_resolve_path);
    ASSERT_PTR_EQ(&flop_word_four, mock_resolve_len);   /* pea (0xac,PC) */
    ASSERT_EQ(4, *mock_resolve_len);
    ASSERT_EQ(1, mock_set_dad_calls);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, mock_dismount_calls);
    ASSERT_EQ(0, mock_dropu_calls);
}

/* Same, but "/flp" is some other object: the 0xE0003 stands, the volume is
 * dismounted, and - because this call did not add the entry - nothing is
 * dropped (D2 = 0 at 0x00E32482). */
TEST(existing_link_to_other_volume_dismounts_without_drop)
{
    status_$t status = 0x55555555;

    reset();
    mock_addu_status = status_$name_already_exists;
    mock_resolve_uid.low = 0x99999999u;
    flop_$mount_floppy(&status);

    ASSERT_EQ(status_$name_already_exists, status);
    ASSERT_EQ(0, mock_set_dad_calls);
    ASSERT_EQ(1, mock_dismount_calls);
    ASSERT_EQ(0, mock_dropu_calls);
}

/* Same again when NAME_$RESOLVE itself fails (0x00E3249E). */
TEST(existing_link_resolve_failure_dismounts_without_drop)
{
    status_$t status = 0x55555555;

    reset();
    mock_addu_status = status_$name_already_exists;
    mock_resolve_status = 0x000E0002;
    flop_$mount_floppy(&status);

    ASSERT_EQ(status_$name_already_exists, status);
    ASSERT_EQ(0, mock_set_dad_calls);
    ASSERT_EQ(1, mock_dismount_calls);
    ASSERT_EQ(0, mock_dropu_calls);
}

/* 0x00E324BA: any other ADDU error goes straight to cleanup, no drop. */
TEST(other_addu_error_dismounts_without_drop)
{
    status_$t status = 0x55555555;

    reset();
    mock_addu_status = 0x000E0007;
    flop_$mount_floppy(&status);

    ASSERT_EQ(0x000E0007, status);
    ASSERT_EQ(0, mock_resolve_calls);
    ASSERT_EQ(0, mock_set_dad_calls);
    ASSERT_EQ(1, mock_dismount_calls);
    ASSERT_EQ(0, mock_dropu_calls);
}

/* 0x00E324D4-0x00E324E0: a write-protected disk from SET_DAD (bit 31 set or
 * not) is forgiven, and the mount status is what comes back. */
TEST(set_dad_write_protected_is_forgiven)
{
    status_$t status = 0x55555555;

    reset();
    mock_set_dad_status = (status_$t)(0x80000000u | status_$disk_write_protected);
    flop_$mount_floppy(&status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, mock_dismount_calls);
}

/* 0x00E324E6-0x00E32528: a SET_DAD failure after this call added the entry
 * dismounts AND drops "flp" again (D2 = 0xFF from 0x00E32476); the drop's
 * own status goes to the local, not to *status_ret. */
TEST(set_dad_error_dismounts_and_drops)
{
    status_$t status = 0x55555555;

    reset();
    mock_set_dad_status = 0x000E0009;
    flop_$mount_floppy(&status);

    ASSERT_EQ(0x000E0009, status);
    ASSERT_EQ(1, mock_dismount_calls);
    ASSERT_PTR_EQ(&flop_word_one, mock_dismount_dev);       /* pea (0x42,PC) */
    ASSERT_PTR_EQ(&flop_zero_word, mock_dismount_bus);
    ASSERT_PTR_EQ(&flop_zero_word, mock_dismount_ctlr);     /* pea (0x46,PC) */
    ASSERT_PTR_EQ(&flop_word_one, mock_dismount_lv);        /* pea (0x4c,PC) */
    ASSERT_PTR_EQ(&flop_zero_byte, mock_dismount_force);    /* pea (0x4c,PC) */
    ASSERT_EQ(mock_volume_uid.low, mock_dismount_uid.low);
    ASSERT_EQ(1, mock_dropu_calls);
    ASSERT_EQ(mock_node_uid.low, mock_dropu_dir.low);
    ASSERT_PTR_EQ(flop_flp_name, mock_dropu_name);
    ASSERT_PTR_EQ((uint16_t *)&flop_flp_name_len, mock_dropu_len);
    ASSERT_EQ(mock_volume_uid.low, mock_dropu_entry.low);
}

int main(void)
{
    printf("flop_$mount_floppy tests\n");
    RUN_TEST(constant_cells);
    RUN_TEST(mount_arguments);
    RUN_TEST(success_path);
    RUN_TEST(mount_error_returns_immediately);
    RUN_TEST(write_protected_warning_is_forgiven_and_returned);
    RUN_TEST(existing_link_to_same_volume_is_ok);
    RUN_TEST(existing_link_to_other_volume_dismounts_without_drop);
    RUN_TEST(existing_link_resolve_failure_dismounts_without_drop);
    RUN_TEST(other_addu_error_dismounts_without_drop);
    RUN_TEST(set_dad_write_protected_is_forgiven);
    RUN_TEST(set_dad_error_dismounts_and_drops);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
