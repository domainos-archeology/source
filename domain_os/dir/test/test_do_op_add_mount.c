/*
 * dir/test/test_do_op_add_mount.c - unit tests for dir_$do_op_add_mount
 * (0x00E5325E) and dir_$do_op_drop_mount (0x00E533E6).
 *
 * Covers what bead source-xtxa and bead source-r98o reported: the mount
 * tables are ONE-BASED with the node ids based at A5+0x15D8, and the cache
 * invalidation walk is `moveq #0x6e,D0` + `dbf`, i.e. exactly 111 records.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* The DIR module block                                                  */
/* ------------------------------------------------------------------ */

/* A5 = 0x00E7DC00 is the block + 8; the entry cache and the mount tables
 * are its fields. */
MODULE_DATA_DEFINE(dir_$data_t, DIR_$DATA, 0x00E7DBF8);

uid_t UID_$NIL = { 0, 0 };
ml_$exclusion_t DIR_$MUTEX;

static int mock_enter_super, mock_exit_super;
static int mock_excl_start, mock_excl_stop;
static status_$t mock_open_status;
static uint8_t mock_cattr_sub_type;
static status_$t mock_cattr_status;
static int mock_release_calls;

void ACL_$ENTER_SUPER(void) { mock_enter_super++; }
void ACL_$EXIT_SUPER(void)  { mock_exit_super++; }
void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; mock_excl_start++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { (void)e; mock_excl_stop++; }

void dir_$open_dir(void *uid, int16_t mode, int16_t rights,
                   void *handle_ret, status_$t *status_ret)
{
    (void)uid; (void)mode; (void)rights;
    *(uint32_t *)handle_ret = 0;
    *status_ret = mock_open_status;
}

void dir_$release_handle(void *handle_ptr)
{
    (void)handle_ptr;
    mock_release_calls++;
}

void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    (void)loc_rec; (void)flags;
    memset(attrs, 0, sizeof(*attrs));
    attrs->sub_type = mock_cattr_sub_type;
    *status = mock_cattr_status;
}

#include "../do_op_add_mount.c"
#include "../do_op_drop_mount.c"

static void reset_mocks(void)
{
    memset(&DIR_$DATA, 0, sizeof(DIR_$DATA));
    mock_enter_super = mock_exit_super = 0;
    mock_excl_start = mock_excl_stop = 0;
    mock_open_status = status_$ok;
    mock_cattr_sub_type = 0;
    mock_cattr_status = status_$ok;
    mock_release_calls = 0;
}

static uint32_t *mount_uid_slot(int n)
{
    return &DIR_$DATA.mount_uid[n].high;
}
static uint32_t *mount_tgt_slot(int n)
{
    return &DIR_$DATA.mount_tgt[n].high;
}
static uint32_t *mount_node_slot(int n)
{
    return &DIR_$DATA.mount_node[n];
}

/* ------------------------------------------------------------------ */

/*
 * 0x00E53362-0x00E53394: the new entry is written at index count + 1, with
 * the node id at A5 + 0x15D8 + 4*(count + 1) - one slot below the base the
 * tree used to use.
 */
TEST(add_mount_writes_the_one_based_slot)
{
    uid_t dir_uid = { 0xD1, 0xD2 };
    uid_t mnt_uid = { 0xE1, 0xE2 };
    status_$t status;

    reset_mocks();
    dir_$do_op_add_mount(&dir_uid, &mnt_uid, 0x1234, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, DIR_$DATA.mttab_count);
    ASSERT_EQ(0xD1, mount_uid_slot(1)[0]);
    ASSERT_EQ(0xD2, mount_uid_slot(1)[1]);
    ASSERT_EQ(0xE1, mount_tgt_slot(1)[0]);
    ASSERT_EQ(0xE2, mount_tgt_slot(1)[1]);
    ASSERT_EQ(0x1234, *mount_node_slot(1));
    /* Slot 0 of the node table is untouched - the count lives in the uid
     * table's slot 0, not here. */
    ASSERT_EQ(0, *mount_node_slot(0));
    ASSERT_EQ(1, mock_excl_start);
    ASSERT_EQ(1, mock_excl_stop);
    ASSERT_EQ(1, mock_release_calls);
}

/* 0x00E532A0-0x00E532E2: an identical mount is a no-op. */
TEST(add_mount_is_idempotent)
{
    uid_t dir_uid = { 0xD1, 0xD2 };
    uid_t mnt_uid = { 0xE1, 0xE2 };
    status_$t status;

    reset_mocks();
    dir_$do_op_add_mount(&dir_uid, &mnt_uid, 0x1234, &status);
    dir_$do_op_add_mount(&dir_uid, &mnt_uid, 0x1234, &status);

    ASSERT_EQ(1, DIR_$DATA.mttab_count);
    /* The second call never reaches the mutex. */
    ASSERT_EQ(1, mock_excl_start);
}

/* 0x00E53342: the count must stay below DIR_MOUNT_MAX. */
TEST(add_mount_rejects_an_eighth_entry)
{
    uid_t dir_uid = { 0xD1, 0xD2 };
    uid_t mnt_uid = { 0xE1, 0xE2 };
    status_$t status;

    reset_mocks();
    DIR_$DATA.mttab_count = DIR_MOUNT_MAX - 1;
    dir_$do_op_add_mount(&dir_uid, &mnt_uid, 0x1234, &status);

    ASSERT_EQ(status_$directory_is_full, status);
    ASSERT_EQ(DIR_MOUNT_MAX - 1, DIR_$DATA.mttab_count);
}

/*
 * 0x00E53398-0x00E533C2: `moveq #0x6e,D0` + `dbf` walks exactly 111 records
 * of 0x28 bytes starting at A5+0x400.
 *
 * The 111 records occupy A5+0x400 .. A5+0x400 + 0x28*110 + 8 = A5+0x1540,
 * which ends just below the mount tables at A5+0x1554.  A hypothetical
 * record 111 would have its cleared field at 0x28*111 + 0x400 = 0x1558 -
 * the mount COUNT longword itself - and its match field at 0x1560/0x1564.
 * So the walk is bounded above by planting a match there: if the loop ran
 * one record too far it would blow the count away with UID_$NIL.
 */
TEST(add_mount_cache_walk_runs_exactly_111_records)
{
    /* Both halves equal, so that the mount entry the add writes at
     * A5+0x155C/0x1560 supplies the high half of record 111's match. */
    uid_t dir_uid = { 0xD1, 0xD1 };
    uid_t mnt_uid = { 0xE1, 0xE2 };
    status_$t status;
    int j;

    reset_mocks();

    ASSERT_EQ(111, DIR_CACHE_COUNT);
    /* The record just past the last one overlays the mount count. */
    ASSERT_EQ(DIR_MOUNT_COUNT_OFF,
              DIR_DATA_OFF(entry_cache[DIR_CACHE_COUNT]));

    /* Plant the directory uid in every in-range record's match field. */
    for (j = 0; j < DIR_CACHE_COUNT; j++) {
        dir_$entry_cache_t *rec = &DIR_$DATA.entry_cache[j];
        rec->entry_uid.high = 0xD1;
        rec->entry_uid.low = 0xD1;
        rec->dir_uid.high = 0xFFFFFFFFu;
        rec->dir_uid.low = 0xFFFFFFFFu;
    }

    /* The trap for record 111: its match field is A5+0x1560 (which the add
     * fills with dir_uid.low) and A5+0x1564, which nothing else writes. */
    DIR_$DATA.mount_uid[2].high = 0xD1;     /* A5+0x1564 */

    dir_$do_op_add_mount(&dir_uid, &mnt_uid, 0x1234, &status);

    ASSERT_EQ(status_$ok, status);

    /* Every one of the 111 records was cleared to UID_$NIL. */
    for (j = 0; j < DIR_CACHE_COUNT; j++) {
        ASSERT_EQ(0, DIR_$DATA.entry_cache[j].dir_uid.high);
        ASSERT_EQ(0, DIR_$DATA.entry_cache[j].dir_uid.low);
    }

    /* No 112th record: the count and the entry it indexes survive. */
    ASSERT_EQ(1, DIR_$DATA.mttab_count);
    ASSERT_EQ(0xD1, mount_uid_slot(1)[0]);
    ASSERT_EQ(0xD1, mount_uid_slot(1)[1]);
}

/* 0x00E5331E: sub-type 2 means the directory is locked. */
TEST(add_mount_rejects_a_locked_directory)
{
    uid_t dir_uid = { 0xD1, 0xD2 };
    uid_t mnt_uid = { 0xE1, 0xE2 };
    status_$t status;

    reset_mocks();
    mock_cattr_sub_type = 2;
    dir_$do_op_add_mount(&dir_uid, &mnt_uid, 0x1234, &status);

    ASSERT_EQ(status_$naming_directory_locked, status);
    ASSERT_EQ(0, mock_excl_start);
}

/*
 * 0x00E53444-0x00E53486: dropping entry 1 of two moves entry 2 (the last)
 * down into it, node id included, and drops the count.
 */
TEST(drop_mount_moves_the_last_entry_down)
{
    uid_t match = { 0xE1, 0xE2 };
    status_$t status;

    reset_mocks();
    DIR_$DATA.mttab_count = 2;
    mount_uid_slot(1)[0] = 0xA1; mount_uid_slot(1)[1] = 0xA2;
    mount_tgt_slot(1)[0] = 0xE1; mount_tgt_slot(1)[1] = 0xE2;
    *mount_node_slot(1) = 0x1111;
    mount_uid_slot(2)[0] = 0xB1; mount_uid_slot(2)[1] = 0xB2;
    mount_tgt_slot(2)[0] = 0xF1; mount_tgt_slot(2)[1] = 0xF2;
    *mount_node_slot(2) = 0x2222;

    dir_$do_op_drop_mount(&match, 0x9999, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, DIR_$DATA.mttab_count);
    ASSERT_EQ(0xB1, mount_uid_slot(1)[0]);
    ASSERT_EQ(0xF1, mount_tgt_slot(1)[0]);
    ASSERT_EQ(0x2222, *mount_node_slot(1));
}

/* 0x00E53428: the node id alone is enough to match. */
TEST(drop_mount_matches_on_the_node_id_too)
{
    uid_t no_match = { 0x77, 0x88 };
    status_$t status;

    reset_mocks();
    DIR_$DATA.mttab_count = 1;
    mount_tgt_slot(1)[0] = 0xE1; mount_tgt_slot(1)[1] = 0xE2;
    *mount_node_slot(1) = 0x4242;

    dir_$do_op_drop_mount(&no_match, 0x4242, &status);

    ASSERT_EQ(0, DIR_$DATA.mttab_count);
}

int main(void)
{
    printf("=== dir_$do_op_add_mount / drop_mount tests ===\n");
    RUN_TEST(add_mount_writes_the_one_based_slot);
    RUN_TEST(add_mount_is_idempotent);
    RUN_TEST(add_mount_rejects_an_eighth_entry);
    RUN_TEST(add_mount_cache_walk_runs_exactly_111_records);
    RUN_TEST(add_mount_rejects_a_locked_directory);
    RUN_TEST(drop_mount_moves_the_last_entry_down);
    RUN_TEST(drop_mount_matches_on_the_node_id_too);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
