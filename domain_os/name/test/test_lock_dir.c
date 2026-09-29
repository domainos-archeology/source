/*
 * name/test/test_lock_dir.c - Unit tests for NAME_$LOCK_DIR (0x00E54854)
 *
 * The real name/lock_dir.c is #included below and driven through mocks of its
 * callees, so the assertions here exercise the shipping code rather than a
 * re-implementation of it.  Each test targets one of the defects the
 * 2026-09-06 fidelity audit found in this function:
 *
 *   - the signature is five parameters, with lock_mode in the HIGH half of the
 *     longword every caller pushes and acl_rights in the LOW half;
 *   - *handle_ret must receive MST_$MAPS' A0 result (0xE54AD8), not the
 *     address of a dead stack local;
 *   - the status test after MST_$MAPS is `tst.w (0x2,A3)`, i.e. the LOW word
 *     of status_$t (0xE54AE2);
 *   - the six per-process A5-table accesses must actually happen;
 *   - TIME_$WAIT must be called, with delay type 0 and a 6-byte clock value
 *     of { high = 0, low = 0x4000 } (0xE5492E);
 *   - PROC1_$DATA.type[PROC1_$CURRENT] == 9 fails immediately without retrying
 *     (0xE548FC);
 *   - the WDIR and NDIR cached-directory checks must reuse the cached mapping
 *     instead of calling MST_$MAPS (0xE54A2A-0xE54AAE).
 */

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                    */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Globals the code under test refers to                                */
/* ------------------------------------------------------------------ */

#include "name/name_internal.h"

uint32_t TIME_$CLOCKH;
uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
name_$data_t NAME_$DATA;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

/* FILE_$PRIV_LOCK */
static status_$t  mock_lock_status;
static status_$t  mock_lock_status_after_retry;
static int        mock_lock_calls;
static int16_t    mock_lock_asid;
static uint16_t   mock_lock_side;
static uint16_t   mock_lock_mode;
static boolean    mock_lock_local_only;
static uint16_t   mock_lock_flags;
static uint16_t   mock_lock_key;
static uint16_t   mock_lock_rem_wait;
static uint32_t  *mock_lock_slot_io;
static uid_t      mock_lock_uid;

void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t side,
                     uint16_t lock_mode, boolean local_only,
                     uint16_t flags, uint16_t key,
                     uint32_t rem_key, uint32_t rem_node, uint32_t rem_extra,
                     void **acl_ctx, uint16_t rem_wait,
                     uint32_t *slot_io, uint16_t *rights_out,
                     status_$t *status_ret)
{
    (void)rem_key; (void)rem_node; (void)rem_extra; (void)acl_ctx;
    mock_lock_calls++;
    mock_lock_uid = *file_uid;
    mock_lock_asid = asid;
    mock_lock_side = side;
    mock_lock_mode = lock_mode;
    mock_lock_local_only = local_only;
    mock_lock_flags = flags;
    mock_lock_key = key;
    mock_lock_rem_wait = rem_wait;
    mock_lock_slot_io = slot_io;
    *rights_out = 0;
    *status_ret = (mock_lock_calls == 1) ? mock_lock_status
                                         : mock_lock_status_after_retry;
}

/* TIME_$WAIT */
static int      mock_wait_calls;
static uint16_t mock_wait_delay_type;
static uint32_t mock_wait_clock_high;
static uint16_t mock_wait_clock_low;

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    mock_wait_calls++;
    mock_wait_delay_type = *delay_type;
    mock_wait_clock_high = delay->high;
    mock_wait_clock_low = delay->low;
    *status = status_$ok;
}

/* ACL_$RIGHTS / ACL_$ENTER_SUPER / NAME_CONVERT_ACL_STATUS */
static int       mock_rights_calls;
static uint32_t  mock_rights_mask;
static int16_t   mock_rights_obj_type;
static boolean   mock_rights_ignore_super;
static status_$t mock_rights_status;
static int       mock_enter_super_calls;
static int       mock_convert_acl_calls;

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status)
{
    (void)uid;
    mock_rights_calls++;
    mock_rights_ignore_super = *ignore_super;
    mock_rights_mask = *required_mask;
    mock_rights_obj_type = *option_flags;
    *status = mock_rights_status;
    return 0;
}

void ACL_$ENTER_SUPER(void) { mock_enter_super_calls++; }

void NAME_CONVERT_ACL_STATUS(status_$t *status_ret)
{
    mock_convert_acl_calls++;
    *status_ret = 0x000E002E;   /* status_$naming_object_is_not_an_acl_object */
}

/* NAME_$UNLOCK_DIR */
static int mock_unlock_calls;

void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    mock_unlock_calls++;
    *status_ret = status_$ok;
}

/* MST_$MAPS */
static int        mock_maps_calls;
static void      *mock_maps_result;
static status_$t  mock_maps_status;
static int16_t    mock_maps_asid;
static boolean    mock_maps_flags;
static uint32_t   mock_maps_length;
static int16_t    mock_maps_prot;
static int8_t     mock_maps_create;

void *MST_$MAPS(int16_t mode, boolean flags, uid_t *uid, uint32_t offset,
                uint32_t length, int16_t prot, uint32_t hint, boolean create,
                void *out, status_$t *status)
{
    (void)uid; (void)offset; (void)hint; (void)out;
    mock_maps_calls++;
    mock_maps_asid = mode;
    mock_maps_flags = flags;
    mock_maps_length = length;
    mock_maps_prot = prot;
    mock_maps_create = create;
    *status = mock_maps_status;
    return mock_maps_result;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                      */
/* ------------------------------------------------------------------ */

#include "../name_data.c"
#include "../handle_map.c"
#include "../lock_dir.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

#define TEST_PROC   3
#define TEST_ASID   5

static const uid_t TEST_DIR_UID = { 0x11223344u, 0x55667788u };

/* A directory page: the first word is the object type (1 == directory). */
static int16_t dir_page[64];
static int16_t not_a_dir_page[64];

static void reset(void)
{
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    memset(NAME_$OLD_DIR_DATA.lock_slot, 0, sizeof(NAME_$OLD_DIR_DATA.lock_slot));
    memset(NAME_$OLD_DIR_DATA.lock_mode, 0, sizeof(NAME_$OLD_DIR_DATA.lock_mode));
    memset(NAME_$OLD_DIR_DATA.lock_handle, 0, sizeof(NAME_$OLD_DIR_DATA.lock_handle));
    memset(NAME_$OLD_DIR_DATA.lock_uid, 0, sizeof(NAME_$OLD_DIR_DATA.lock_uid));
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));

    PROC1_$CURRENT = TEST_PROC;
    PROC1_$AS_ID = TEST_ASID;
    TIME_$CLOCKH = 0x1000;

    mock_lock_status = status_$ok;
    mock_lock_status_after_retry = status_$ok;
    mock_lock_calls = 0;
    mock_lock_slot_io = NULL;
    mock_wait_calls = 0;
    mock_rights_calls = 0;
    mock_rights_status = status_$ok;
    mock_enter_super_calls = 0;
    mock_convert_acl_calls = 0;
    mock_unlock_calls = 0;
    mock_maps_calls = 0;
    mock_maps_status = status_$ok;
    mock_maps_result = dir_page;

    dir_page[0] = 1;            /* object type: directory */
    not_a_dir_page[0] = 2;      /* anything else */
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/*
 * The two words at (0x10,A6) and (0x12,A6) are separate parameters: the
 * caller's `move.l #0x00040002,-(SP)` means lock_mode = 4, acl_rights = 2.
 * lock_mode reaches FILE_$PRIV_LOCK and NAME_$OLD_DIR_DATA.lock_mode; acl_rights drives
 * ACL_$RIGHTS and is zero-extended to a longword (0xE54976).
 */
TEST(signature_splits_mode_and_rights)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    NAME_$LOCK_DIR(&uid, &handle, 4, 2, &status);

    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(4, mock_lock_mode);                       /* not 2 */
    ASSERT_EQ(4, NAME_$OLD_DIR_DATA.lock_mode[TEST_PROC]);
    ASSERT_EQ(1, mock_rights_calls);
    ASSERT_EQ(2, mock_rights_mask);                     /* not 4 */
    ASSERT_EQ(1, mock_rights_obj_type);                 /* ACL object type = directory */
    /* 0xE54B28 holds a zero byte: the super-user bypass is NOT suppressed. */
    ASSERT_EQ(0, (unsigned char)mock_rights_ignore_super);
    ASSERT_EQ(status_$ok, status);
}

/* acl_rights == 0 skips ACL_$RIGHTS entirely (0xE5496C tst.w D3w). */
TEST(zero_acl_rights_skips_rights_check)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(0, mock_rights_calls);
    ASSERT_EQ(1, mock_enter_super_calls);
    ASSERT_EQ(status_$ok, status);
}

/*
 * 0xE54AD8/0xE54AE0: *handle_ret is the value MST_$MAPS returns in A0.
 * Previously the code stored the address of a dead stack buffer here.
 */
TEST(handle_comes_from_mst_maps_result)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(1, mock_maps_calls);
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), handle);
    ASSERT_EQ(TEST_ASID, mock_maps_asid);
    ASSERT_EQ(0xFF, (uint8_t)mock_maps_flags);
    ASSERT_EQ(0x10000, mock_maps_length);
    ASSERT_EQ(0x16, mock_maps_prot);
    ASSERT_EQ((int8_t)0xFF, mock_maps_create);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, mock_unlock_calls);
}

/*
 * 0xE54AE2 is `tst.w (0x2,A3)`: only the LOW word of the status decides
 * whether MST_$MAPS failed.  A status whose high half is set but whose low
 * half is zero must be accepted.
 */
TEST(mst_maps_status_tests_low_word_only)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    mock_maps_status = 0x00120000;      /* high half set, low half zero */
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(0x00120000, status);      /* not turned into an error */
    ASSERT_EQ(0, mock_unlock_calls);
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), NAME_$OLD_DIR_DATA.lock_handle[TEST_PROC]);

    reset();
    mock_maps_status = 0x00000007;      /* low half set */
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ((status_$t)0x80000007u, status);   /* bset.b #7,(A3) */
    ASSERT_EQ(1, mock_unlock_calls);
}

/* The six per-process A5-table accesses (0xE54894, 0xE548A0, 0xE548AC,
 * 0xE548EA/0xE548EE, 0xE54B02). */
TEST(per_process_tables_are_written)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    NAME_$OLD_DIR_DATA.lock_handle[TEST_PROC] = 0xAAAAAAAA;
    NAME_$LOCK_DIR(&uid, &handle, 7, 0, &status);

    /* A5+0x13E: the requested lock mode */
    ASSERT_EQ(7, NAME_$OLD_DIR_DATA.lock_mode[TEST_PROC]);
    /* A5+0x3C: the slot passed to FILE_$PRIV_LOCK by reference */
    ASSERT_EQ((uintptr_t)&NAME_$OLD_DIR_DATA.lock_slot[TEST_PROC], (uintptr_t)mock_lock_slot_io);
    /* A5+0x2B8: the UID of the directory now locked */
    ASSERT_EQ(TEST_DIR_UID.high, NAME_$OLD_DIR_DATA.lock_uid[TEST_PROC].high);
    ASSERT_EQ(TEST_DIR_UID.low, NAME_$OLD_DIR_DATA.lock_uid[TEST_PROC].low);
    /* A5+0x1BC: cleared on entry, then set to the mapped base */
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), NAME_$OLD_DIR_DATA.lock_handle[TEST_PROC]);
    /* neighbouring slots are untouched */
    ASSERT_EQ(0, NAME_$OLD_DIR_DATA.lock_mode[TEST_PROC + 1]);
    ASSERT_EQ(0, NAME_$OLD_DIR_DATA.lock_uid[TEST_PROC + 1].high);
}

/*
 * A failing FILE_$PRIV_LOCK leaves the per-process UID slot alone (only the
 * success path at 0xE548DE writes it) and the error gets bit 31 set.
 */
TEST(lock_failure_sets_high_bit_and_leaves_uid_clear)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    mock_lock_status = 0x000F0002;
    mock_lock_status_after_retry = 0x000F0002;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ((status_$t)0x800F0002u, status);
    ASSERT_EQ(0, NAME_$OLD_DIR_DATA.lock_uid[TEST_PROC].high);
    ASSERT_EQ(1, mock_enter_super_calls);
    ASSERT_EQ(0, mock_maps_calls);
    ASSERT_EQ(0, mock_unlock_calls);
}

/*
 * file_$object_in_use retries after TIME_$WAIT; the delay is the 6-byte
 * clock { high = 0, low = 0x4000 } and the delay type is the zero word at
 * 0xE5472E (0xE5492E-0xE54944).
 */
TEST(busy_directory_waits_then_retries)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    mock_lock_status = 0x000F0006;              /* file_$object_in_use */
    mock_lock_status_after_retry = status_$ok;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(2, mock_lock_calls);
    ASSERT_EQ(1, mock_wait_calls);
    ASSERT_EQ(0, mock_wait_delay_type);
    ASSERT_EQ(0, mock_wait_clock_high);
    ASSERT_EQ(0x4000, mock_wait_clock_low);
    ASSERT_EQ(status_$ok, status);
}

/*
 * 0xE548FC: a type-9 (server) process never waits - it fails immediately with
 * status_$naming_directory_locked and TIME_$WAIT is not called.
 */
TEST(server_process_does_not_retry)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    PROC1_$DATA.type[TEST_PROC] = 9;
    mock_lock_status = 0x000F0006;
    mock_lock_status_after_retry = status_$ok;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(0, mock_wait_calls);
    /* 0xE54956: status_$naming_directory_locked is returned without bit 31 */
    ASSERT_EQ(0x000E0016, status);

    /* The neighbouring 1-based slot must not be the one consulted. */
    reset();
    PROC1_$DATA.type[TEST_PROC - 1] = 9;
    mock_lock_status = 0x000F0006;
    mock_lock_status_after_retry = status_$ok;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);
    ASSERT_EQ(2, mock_lock_calls);
    ASSERT_EQ(1, mock_wait_calls);
}

/* The retry budget is 0x78 attempts (0xE5490E-0xE54914). */
TEST(retry_budget_is_0x78)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    mock_lock_status = 0x000F0006;
    mock_lock_status_after_retry = 0x000F0006;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(0x000E0016, status);
    ASSERT_EQ(0x78, mock_wait_calls);
    ASSERT_EQ(0x79, mock_lock_calls);
}

/* 0xE549B2 / 0xE549EE: the NODE and COM cached mappings short-circuit
 * MST_$MAPS when active < 0, reserved_02 == 0 and entry_count == 1. */
TEST(node_and_com_cached_mappings)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    NAME_$DATA.node_uid = TEST_DIR_UID;
    NAME_$DATA.node_mapped_info.active = -1;
    NAME_$DATA.node_mapped_info.reserved_02 = 0;
    NAME_$DATA.node_mapped_info.entry_count = 1;
    NAME_$DATA.node_mapped_info.first_base = NAME_$PTR_TO_HANDLE(dir_page);
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);
    ASSERT_EQ(0, mock_maps_calls);
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), handle);

    /* entry_count != 1 disqualifies the cache */
    reset();
    NAME_$DATA.node_uid = TEST_DIR_UID;
    NAME_$DATA.node_mapped_info.active = -1;
    NAME_$DATA.node_mapped_info.entry_count = 2;
    NAME_$DATA.node_mapped_info.first_base = 0x999;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);
    ASSERT_EQ(1, mock_maps_calls);

    reset();
    NAME_$DATA.com_uid = TEST_DIR_UID;
    NAME_$DATA.com_mapped_info.active = -1;
    NAME_$DATA.com_mapped_info.entry_count = 1;
    NAME_$DATA.com_mapped_info.first_base = NAME_$PTR_TO_HANDLE(dir_page);
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);
    ASSERT_EQ(0, mock_maps_calls);
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), handle);
}

/* 0xE54A2A-0xE54A6A: the per-ASID working-directory cache. */
TEST(wdir_cached_mapping)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    NAME_$DATA.wdir_uid[TEST_ASID] = TEST_DIR_UID;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].active = -1;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].reserved_02 = 0;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].entry_count = 1;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].first_base = NAME_$PTR_TO_HANDLE(dir_page);
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(0, mock_maps_calls);
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), handle);
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), NAME_$OLD_DIR_DATA.lock_handle[TEST_PROC]);

    /* The UID must match this ASID's slot, not a neighbour's. */
    reset();
    NAME_$DATA.wdir_uid[TEST_ASID + 1] = TEST_DIR_UID;
    NAME_$DATA.wdir_mapped_info[TEST_ASID + 1].active = -1;
    NAME_$DATA.wdir_mapped_info[TEST_ASID + 1].entry_count = 1;
    NAME_$DATA.wdir_mapped_info[TEST_ASID + 1].first_base = 0x999;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);
    ASSERT_EQ(1, mock_maps_calls);

    /* reserved_02 must be zero for the cache to be reused. */
    reset();
    NAME_$DATA.wdir_uid[TEST_ASID] = TEST_DIR_UID;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].active = -1;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].reserved_02 = 1;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].entry_count = 1;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].first_base = 0x999;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);
    ASSERT_EQ(1, mock_maps_calls);
}

/* 0xE54A6C-0xE54AA4: the per-ASID naming-directory cache, reached when the
 * WDIR check does not match. */
TEST(ndir_cached_mapping)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    NAME_$DATA.ndir_uid[TEST_ASID] = TEST_DIR_UID;
    NAME_$DATA.ndir_mapped_info[TEST_ASID].active = -1;
    NAME_$DATA.ndir_mapped_info[TEST_ASID].reserved_02 = 0;
    NAME_$DATA.ndir_mapped_info[TEST_ASID].entry_count = 1;
    NAME_$DATA.ndir_mapped_info[TEST_ASID].first_base = NAME_$PTR_TO_HANDLE(dir_page);
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(0, mock_maps_calls);
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), handle);

    /*
     * A WDIR UID match whose cached mapping is unusable still falls through to
     * the NDIR check (0xE54A6A does not skip it).
     */
    reset();
    NAME_$DATA.wdir_uid[TEST_ASID] = TEST_DIR_UID;
    NAME_$DATA.wdir_mapped_info[TEST_ASID].active = 0;      /* not active */
    NAME_$DATA.ndir_uid[TEST_ASID] = TEST_DIR_UID;
    NAME_$DATA.ndir_mapped_info[TEST_ASID].active = -1;
    NAME_$DATA.ndir_mapped_info[TEST_ASID].entry_count = 1;
    NAME_$DATA.ndir_mapped_info[TEST_ASID].first_base = NAME_$PTR_TO_HANDLE(dir_page);
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);
    ASSERT_EQ(0, mock_maps_calls);
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(dir_page), handle);
}

/* 0xE54B06: the mapped object must have type 1 or the lock is released. */
TEST(non_directory_is_rejected_and_unlocked)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    mock_maps_result = not_a_dir_page;
    NAME_$LOCK_DIR(&uid, &handle, 1, 0, &status);

    ASSERT_EQ(0x000E000D, status);      /* status_$naming_bad_directory */
    ASSERT_EQ(1, mock_unlock_calls);
    /* the handle was still recorded before the type check (0xE54B02) */
    ASSERT_EQ(NAME_$PTR_TO_HANDLE(not_a_dir_page), NAME_$OLD_DIR_DATA.lock_handle[TEST_PROC]);
}

/* An ACL failure converts the status and releases the lock (0xE5499A). */
TEST(acl_failure_converts_status_and_unlocks)
{
    uid_t uid = TEST_DIR_UID;
    uint32_t handle = 0;
    status_$t status = 0xdeadbeef;

    reset();
    mock_rights_status = 0x00130001;
    NAME_$LOCK_DIR(&uid, &handle, 4, 2, &status);

    ASSERT_EQ(1, mock_convert_acl_calls);
    ASSERT_EQ(1, mock_unlock_calls);
    ASSERT_EQ(0, mock_maps_calls);
    ASSERT_EQ(1, mock_enter_super_calls);
}

int main(void)
{
    printf("NAME_$LOCK_DIR tests\n");
    RUN_TEST(signature_splits_mode_and_rights);
    RUN_TEST(zero_acl_rights_skips_rights_check);
    RUN_TEST(handle_comes_from_mst_maps_result);
    RUN_TEST(mst_maps_status_tests_low_word_only);
    RUN_TEST(per_process_tables_are_written);
    RUN_TEST(lock_failure_sets_high_bit_and_leaves_uid_clear);
    RUN_TEST(busy_directory_waits_then_retries);
    RUN_TEST(server_process_does_not_retry);
    RUN_TEST(retry_budget_is_0x78);
    RUN_TEST(node_and_com_cached_mappings);
    RUN_TEST(wdir_cached_mapping);
    RUN_TEST(ndir_cached_mapping);
    RUN_TEST(non_directory_is_rejected_and_unlocked);
    RUN_TEST(acl_failure_converts_status_and_unlocks);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
