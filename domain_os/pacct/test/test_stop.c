/*
 * pacct/test/test_stop.c - PACCT_$STOP (0x00E5A8C0)
 *
 * The two deviations source-0tzz is about:
 *   - 0x00E5A920 `move.l #0x230002,(-0x74,A6)` stores a status into the
 *     routine's own frame cell on the no-privilege path.  Nothing reads it
 *     back (PACCT_$STOP has no status parameter and no return value), so the
 *     only externally visible consequence is that ACL_$GET_EXSID's status
 *     cell is not left holding zero.
 *   - when pacct_owner is already UID_$NIL the image branches to 0x00E5A990
 *     and STILL executes the "pacct_owner = UID_$NIL" store; it skips only
 *     the unmap and the unlock.  There is no early return.
 *
 * The real pacct/stop.c is #included at the bottom; everything it calls is
 * mocked here.
 */

#include <stdio.h>
#include <string.h>

#include "pacct/pacct_internal.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

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

/* ==========================================================================
 * Globals PACCT_$STOP reaches through A5 = 0xE817EC
 * ========================================================================== */

pacct_state_t pacct_state;
uid_t UID_$NIL = { 0, 0 };
uid_t RGYC_$G_LOCKSMITH_UID = { 0x00AA0001u, 0x00BB0002u };

/* ==========================================================================
 * Mock bookkeeping
 * ========================================================================== */

#define LOCKSMITH_HI 0x00AA0001u
#define LOCKSMITH_LO 0x00BB0002u

static int       exsid_calls;
static status_$t exsid_status;
static uid_t     exsid_user, exsid_group, exsid_org, exsid_login;

static int       unmap_calls;
static uint32_t  unmap_start;
static uint32_t  unmap_size;
static int16_t   unmap_mode;

static int       unlock_calls;
static uid_t    *unlock_uid;
static int32_t   unlock_slot;
static uint16_t  unlock_mode;

/* ==========================================================================
 * Mocks
 * ========================================================================== */

void ACL_$GET_EXSID(void *exsid, status_$t *status)
{
    uid_t *p = (uid_t *)exsid;

    exsid_calls++;
    p[0] = exsid_user;
    p[1] = exsid_group;
    p[2] = exsid_org;
    p[3] = exsid_login;
    *status = exsid_status;
}

void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status)
{
    (void)uid; (void)asid;
    unmap_calls++;
    unmap_mode  = mode;
    unmap_start = start;
    unmap_size  = size;
    *status = status_$ok;
}

boolean FILE_$PRIV_UNLOCK(uid_t *file_uid, int32_t lock_slot,
                          uint16_t lock_mode, uint16_t asid,
                          boolean by_key, uint16_t key,
                          uint32_t rem_key, uint32_t rem_node,
                          uint32_t *dtv_out, status_$t *status_ret)
{
    (void)asid; (void)by_key; (void)key; (void)rem_key; (void)rem_node;
    unlock_calls++;
    unlock_uid  = file_uid;
    unlock_slot = lock_slot;
    unlock_mode = lock_mode;
    *dtv_out = 0;
    *status_ret = status_$ok;
    return 0;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../stop.c"

static uint32_t host_buffer[4];

static void reset(void)
{
    memset(&pacct_state, 0, sizeof(pacct_state));
    exsid_calls = 0;
    exsid_status = status_$ok;
    /* by default nobody is the locksmith */
    exsid_user  = (uid_t){ 1, 1 };
    exsid_group = (uid_t){ 2, 2 };
    exsid_org   = (uid_t){ 3, 3 };
    exsid_login = (uid_t){ 4, 4 };

    unmap_calls = 0; unmap_start = 0; unmap_size = 0; unmap_mode = 0;
    unlock_calls = 0; unlock_uid = NULL; unlock_slot = 0; unlock_mode = 0;
}

static void be_locksmith(void)
{
    exsid_login.high = LOCKSMITH_HI;
    exsid_login.low  = LOCKSMITH_LO;
}

static void enable_accounting(void)
{
    pacct_state.owner.high  = 0x12345678u;
    pacct_state.owner.low   = 0x9ABCDEF0u;
    pacct_state.lock_handle = 0x00000042u;
    pacct_state.buf_remaining = 0x1000;
    pacct_state.map_offset  = 0x2000;
    pacct_state.map_ptr     = host_buffer;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E5A8DC: any ACL_$GET_EXSID failure returns before the UID compares. */
TEST(get_exsid_failure_returns)
{
    reset();
    enable_accounting();
    exsid_status = 0x00230001;
    PACCT_$STOP();

    ASSERT_EQ(1, exsid_calls);
    ASSERT_EQ(0, unmap_calls);
    ASSERT_EQ(0, unlock_calls);
    /* accounting is left running */
    ASSERT_EQ(0x12345678u, pacct_state.owner.high);
}

/*
 * The first deviation: 0x00E5A920 stores 0x230002 into the frame's status
 * cell before returning.  The cell is the same one ACL_$GET_EXSID reported
 * into, so a caller that could see it would see the rights error and not the
 * zero ACL_$GET_EXSID left there.
 */
TEST(no_privilege_stores_the_rights_status)
{
    reset();
    enable_accounting();
    PACCT_$STOP();

    ASSERT_EQ(0x00230002, status_$insufficient_rights_to_perform_operation);
    /* nothing is torn down and accounting keeps running */
    ASSERT_EQ(0, unmap_calls);
    ASSERT_EQ(0, unlock_calls);
    ASSERT_EQ(0x12345678u, pacct_state.owner.high);
    ASSERT_EQ(0x9ABCDEF0u, pacct_state.owner.low);
}

/* 0x00E5A8E4/0x00E5A8F8/0x00E5A90C: login, then group, then user. */
TEST(any_of_three_sids_may_be_the_locksmith)
{
    reset();
    exsid_group.high = LOCKSMITH_HI; exsid_group.low = LOCKSMITH_LO;
    enable_accounting();
    PACCT_$STOP();
    ASSERT_EQ(1, unlock_calls);

    reset();
    exsid_user.high = LOCKSMITH_HI; exsid_user.low = LOCKSMITH_LO;
    enable_accounting();
    PACCT_$STOP();
    ASSERT_EQ(1, unlock_calls);
}

/*
 * The second deviation: with the owner already nil the image jumps to
 * 0x00E5A990 and runs the redundant store, but skips the unmap and unlock.
 */
TEST(already_stopped_still_rewrites_the_owner)
{
    reset();
    be_locksmith();
    pacct_state.owner.high = UID_$NIL.high;
    pacct_state.owner.low  = UID_$NIL.low;
    /* leave a mapped buffer behind to prove the unmap is skipped */
    pacct_state.map_ptr    = host_buffer;
    pacct_state.map_offset = 0x2000;

    PACCT_$STOP();

    ASSERT_EQ(0, unmap_calls);
    ASSERT_EQ(0, unlock_calls);
    /* 0x00E5A964-0x00E5A96E is skipped, so the buffer state is untouched */
    ASSERT_EQ((uintptr_t)host_buffer, (uintptr_t)pacct_state.map_ptr);
    ASSERT_EQ(0x2000, pacct_state.map_offset);
    /* but 0x00E5A990 still runs */
    ASSERT_EQ(UID_$NIL.high, pacct_state.owner.high);
    ASSERT_EQ(UID_$NIL.low, pacct_state.owner.low);
}

/* 0x00E5A93C-0x00E5A98E: the full teardown. */
TEST(running_accounting_is_torn_down)
{
    reset();
    be_locksmith();
    enable_accounting();
    PACCT_$STOP();

    /* 0x00E5A942-0x00E5A95A: mode 1, start by value, size = map_offset */
    ASSERT_EQ(1, unmap_calls);
    ASSERT_EQ(1, unmap_mode);
    ASSERT_EQ(ARCH_PTR_TO_VA(host_buffer), unmap_start);
    ASSERT_EQ(0x2000, unmap_size);

    /* 0x00E5A964-0x00E5A96E */
    ASSERT_EQ(0, (uintptr_t)pacct_state.map_ptr);
    ASSERT_EQ(0, pacct_state.map_offset);
    ASSERT_EQ(0, pacct_state.buf_remaining);

    /* 0x00E5A970-0x00E5A98E: `move.l #0x40000` = mode word 4, asid word 0 */
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ((uintptr_t)&pacct_state.owner, (uintptr_t)unlock_uid);
    ASSERT_EQ(0x42, unlock_slot);
    ASSERT_EQ(4, unlock_mode);

    /* 0x00E5A990 */
    ASSERT_EQ(UID_$NIL.high, pacct_state.owner.high);
    ASSERT_EQ(UID_$NIL.low, pacct_state.owner.low);
}

/* 0x00E5A93C `tst.l (0x18,A5)`: no buffer mapped, no unmap - but the three
 * clears and the unlock still run. */
TEST(unmapped_buffer_skips_only_the_unmap)
{
    reset();
    be_locksmith();
    enable_accounting();
    pacct_state.map_ptr = NULL;

    PACCT_$STOP();

    ASSERT_EQ(0, unmap_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, pacct_state.map_offset);
    ASSERT_EQ(0, pacct_state.buf_remaining);
    ASSERT_EQ(UID_$NIL.high, pacct_state.owner.high);
}

int main(void)
{
    printf("PACCT_$STOP tests\n");
    RUN_TEST(get_exsid_failure_returns);
    RUN_TEST(no_privilege_stores_the_rights_status);
    RUN_TEST(any_of_three_sids_may_be_the_locksmith);
    RUN_TEST(already_stopped_still_rewrites_the_owner);
    RUN_TEST(running_accounting_is_torn_down);
    RUN_TEST(unmapped_buffer_skips_only_the_unmap);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
