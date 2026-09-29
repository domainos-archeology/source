/*
 * acl/test/test_rights.c - Unit tests for ACL_$RIGHTS (0x00E46A00)
 *
 * The real acl/rights.c is #included below and driven through a mock of
 * acl_$eval_rights (0x00E464B8), so the assertions exercise the shipping
 * code.  The behaviours covered are the ones bead source-6vl5 raised:
 *
 *   - the second parameter is a POINTER TO A DOMAIN BOOLEAN and is
 *     DEREFERENCED (0x00E46A50 `movea.l (0xc,A6),A3` / 0x00E46A54
 *     `move.b (A3),-(SP)`), so the byte at that address - not the pointer -
 *     reaches acl_$eval_rights;
 *   - the rights mask is read as a LONGWORD (0x00E46A4E `move.l (A2)`) and
 *     the option flags as a WORD (0x00E46A48 `move.w (A1)`);
 *   - the object UID is COPIED into ACL_$RIGHTS' own frame (0x00E46A12) and
 *     the copy, not the caller's pointer, is what acl_$eval_rights sees;
 *   - the two privilege booleans come from `tst.w` + `sgt` on
 *     ACL_$UNWIRED_DATA.super_count[cur] (0x00E46A3C) and ACL_$DATA.subsys_level[cur]
 *     (0x00E46A30), i.e. 0xFF when strictly greater than zero;
 *   - the SID block is ACL_$DATA.current_sids[cur] (0xE90D10 + cur*0x24) and the
 *     project list is 0xE924FC + cur*0x40 = &ACL_$DATA.proj_uids[cur][0]
 *     (source-4h7g);
 *   - the longword acl_$eval_rights leaves in D0 is ACL_$RIGHTS' own result
 *     (callers test it with `tst.l` / `cmpi.l`, e.g. 0x00E71504).
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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

/* ------------------------------------------------------------------ */
/* Globals the code under test refers to                                */
/* ------------------------------------------------------------------ */

#include "acl/acl_internal.h"

uint16_t        PROC1_$CURRENT;
MODULE_DATA_DEFINE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);
MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);

/* ------------------------------------------------------------------ */
/* Mock of acl_$eval_rights (0x00E464B8)                                */
/* ------------------------------------------------------------------ */

static int              ev_calls;
static acl_sid_block_t *ev_sids;
static uid_t           *ev_proj_uids;
static uid_t           *ev_uid_ptr;
static uid_t            ev_uid_value;
static boolean          ev_ignore_super;
static uint32_t         ev_required_mask;
static int16_t          ev_option_flags;
static boolean          ev_in_super;
static boolean          ev_in_subsys;
static status_$t       *ev_status_ptr;

static uint32_t         ev_result;
static status_$t        ev_status_out;

uint32_t acl_$eval_rights(acl_sid_block_t *sids, uid_t *proj_uids, uid_t *uid,
                          boolean ignore_super, uint32_t required_mask,
                          int16_t option_flags, boolean in_super,
                          boolean in_subsys, status_$t *status_ret)
{
    ev_calls++;
    ev_sids          = sids;
    ev_proj_uids     = proj_uids;
    ev_uid_ptr       = uid;
    ev_uid_value     = *uid;
    ev_ignore_super  = ignore_super;
    ev_required_mask = required_mask;
    ev_option_flags  = option_flags;
    ev_in_super      = in_super;
    ev_in_subsys     = in_subsys;
    ev_status_ptr    = status_ret;

    *status_ret = ev_status_out;
    return ev_result;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                      */
/* ------------------------------------------------------------------ */

#include "../rights.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

#define TEST_PID    7

static const uid_t TEST_UID = { 0x12345678u, 0x9ABCDEF0u };

static void reset(void)
{
    ev_calls         = 0;
    ev_sids          = NULL;
    ev_proj_uids     = NULL;
    ev_uid_ptr       = NULL;
    ev_uid_value.high = 0;
    ev_uid_value.low  = 0;
    ev_ignore_super  = 0;
    ev_required_mask = 0;
    ev_option_flags  = 0;
    ev_in_super      = 0;
    ev_in_subsys     = 0;
    ev_status_ptr    = NULL;
    ev_result        = 0;
    ev_status_out    = status_$ok;

    memset(ACL_$DATA.current_sids, 0, sizeof(ACL_$DATA.current_sids));
    memset(ACL_$DATA.proj_uids, 0, sizeof(ACL_$DATA.proj_uids));
    memset(ACL_$UNWIRED_DATA.super_count, 0, sizeof(ACL_$UNWIRED_DATA.super_count));
    memset(ACL_$DATA.subsys_level, 0, sizeof(ACL_$DATA.subsys_level));

    PROC1_$CURRENT = TEST_PID;
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/*
 * 0x00E46A50-0x00E46A54: the second argument is dereferenced.  A caller that
 * passed NULL here (several did before source-6vl5) would fault on the
 * target; a caller that passes a byte cell hands over that byte's VALUE.
 */
TEST(ignore_super_is_dereferenced_true)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = true;              /* 0xFF, as at 0x00E5879A */
    uint32_t  mask = 0x00000048u;
    int16_t   opts = 1;
    status_$t status = 0xdeadbeef;

    reset();
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);

    ASSERT_EQ(1, ev_calls);
    ASSERT_EQ(0xFF, (unsigned char)ev_ignore_super);
}

TEST(ignore_super_is_dereferenced_false)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = false;             /* 0x00, as at 0x00E54B28 */
    uint32_t  mask = 0x00000002u;
    int16_t   opts = 0;
    status_$t status = 0xdeadbeef;

    reset();
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);

    ASSERT_EQ(1, ev_calls);
    ASSERT_EQ(0x00, (unsigned char)ev_ignore_super);
}

/*
 * 0x00E46A4E `move.l (A2),-(SP)` and 0x00E46A48 `move.w (A1),-(SP)`: the
 * mask is a full longword, the option flags only a word.
 */
TEST(mask_is_a_longword_and_options_a_word)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = false;
    uint32_t  mask = 0xFFFFFFFFu;       /* as at 0x00E5EE84 / 0x00E73E40 */
    int16_t   opts = -1;                /* as at 0x00E505C4 */
    status_$t status = 0xdeadbeef;

    reset();
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);

    ASSERT_EQ(0xFFFFFFFFu, ev_required_mask);
    ASSERT_EQ((uint16_t)0xFFFF, (uint16_t)ev_option_flags);
}

/*
 * 0x00E46A0E-0x00E46A18: the UID is copied into ACL_$RIGHTS' own frame and
 * the ADDRESS OF THE COPY is what acl_$eval_rights receives.
 */
TEST(uid_is_copied_into_the_local_frame)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = false;
    uint32_t  mask = 1;
    int16_t   opts = 1;
    status_$t status = 0xdeadbeef;

    reset();
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);

    ASSERT_TRUE(ev_uid_ptr != &uid);
    ASSERT_EQ(TEST_UID.high, ev_uid_value.high);
    ASSERT_EQ(TEST_UID.low,  ev_uid_value.low);
}

/*
 * 0x00E46A3C `tst.w (0xb76,A1)` + `sgt` and 0x00E46A30 `tst.w (-0x3d5a,A1)`
 * + `sgt`: strictly-greater-than-zero, yielding a Domain boolean.
 */
TEST(privilege_booleans_are_sgt_of_the_per_process_counters)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = false;
    uint32_t  mask = 1;
    int16_t   opts = 1;
    status_$t status = 0xdeadbeef;

    reset();
    ACL_$UNWIRED_DATA.super_count[TEST_PID]  = 0;
    ACL_$DATA.subsys_level[TEST_PID] = 0;
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);
    ASSERT_EQ(0x00, (unsigned char)ev_in_super);
    ASSERT_EQ(0x00, (unsigned char)ev_in_subsys);

    reset();
    ACL_$UNWIRED_DATA.super_count[TEST_PID]  = 1;
    ACL_$DATA.subsys_level[TEST_PID] = 3;
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);
    ASSERT_EQ(0xFF, (unsigned char)ev_in_super);
    ASSERT_EQ(0xFF, (unsigned char)ev_in_subsys);

    /* `sgt` is a SIGNED test: a negative counter is not "in super". */
    reset();
    ACL_$UNWIRED_DATA.super_count[TEST_PID]  = -1;
    ACL_$DATA.subsys_level[TEST_PID] = -1;
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);
    ASSERT_EQ(0x00, (unsigned char)ev_in_super);
    ASSERT_EQ(0x00, (unsigned char)ev_in_subsys);
}

/*
 * 0x00E46A7C `pea (-0x6584,A0)` -> 0xE90D10 + cur*0x24 and 0x00E46A66
 * `pea (-0x4d98,A4)` -> 0xE924FC + cur*0x40, i.e. the first slot of the
 * current process' project list.  ACL_$DATA.proj_uids is declared with the base
 * 0xE924FC that ACL_$INIT proves (0x00E31122-0x00E3113C writes UID_$NIL to
 * eight slots starting at 0xE9253C = 0xE924FC + 1*0x40), so this is [cur][0].
 */
TEST(tables_are_indexed_by_the_current_process)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = false;
    uint32_t  mask = 1;
    int16_t   opts = 1;
    status_$t status = 0xdeadbeef;

    reset();
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);

    ASSERT_TRUE(ev_sids == &ACL_$DATA.current_sids[TEST_PID]);
    ASSERT_TRUE(ev_proj_uids == &ACL_$DATA.proj_uids[TEST_PID][0]);

    /* The image reloads PROC1_$CURRENT for every table; a different process
     * selects a different pair of rows. */
    reset();
    PROC1_$CURRENT = 0;
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);
    ASSERT_TRUE(ev_sids == &ACL_$DATA.current_sids[0]);
    ASSERT_TRUE(ev_proj_uids == &ACL_$DATA.proj_uids[0][0]);
}

/*
 * The project pointer is exactly 0xE924FC + cur*0x40 - the row base, with no
 * 8-byte bias, now that ACL_$DATA.proj_uids is declared at 0xE924FC (source-4h7g).
 */
TEST(project_pointer_is_the_row_base)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = false;
    uint32_t  mask = 1;
    int16_t   opts = 1;
    status_$t status = 0xdeadbeef;

    reset();
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);

    ASSERT_EQ(0, (const char *)ev_proj_uids -
                 (const char *)&ACL_$DATA.proj_uids[TEST_PID][0]);
    ASSERT_EQ(TEST_PID * 0x40,
              (const char *)ev_proj_uids - (const char *)&ACL_$DATA.proj_uids[0][0]);
}

/*
 * ACL_$RIGHTS has no stack cleanup after the `bsr` at 0x00E46A80: D0 falls
 * straight through, so the callee's full longword is the result.  Callers
 * such as AUDIT_$ADMINISTRATOR compare it with `cmpi.l #0x2,D0`.
 */
TEST(result_is_the_full_longword_from_eval_rights)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = false;
    uint32_t  mask = 0xFFFFFFFFu;
    int16_t   opts = 1;
    status_$t status = 0xdeadbeef;

    reset();
    ev_result = 0x00010000u;            /* zero in the low word */
    ASSERT_EQ(0x00010000u, ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status));

    reset();
    ev_result = 2;
    ASSERT_EQ(2u, ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status));
}

/*
 * The caller's status pointer is handed straight through (0x00E46A1A
 * `move.l (0x18,A6),-(SP)`).
 */
TEST(status_pointer_is_passed_through)
{
    uid_t     uid  = TEST_UID;
    boolean   flag = false;
    uint32_t  mask = 1;
    int16_t   opts = 1;
    status_$t status = 0xdeadbeef;

    reset();
    ev_status_out = 0x00230001;         /* status_$no_right_to_perform_operation */
    ACL_$RIGHTS(&uid, &flag, &mask, &opts, &status);

    ASSERT_TRUE(ev_status_ptr == &status);
    ASSERT_EQ(0x00230001u, status);
}

int main(void)
{
    printf("ACL_$RIGHTS (0x00E46A00) tests\n");

    RUN_TEST(ignore_super_is_dereferenced_true);
    RUN_TEST(ignore_super_is_dereferenced_false);
    RUN_TEST(mask_is_a_longword_and_options_a_word);
    RUN_TEST(uid_is_copied_into_the_local_frame);
    RUN_TEST(privilege_booleans_are_sgt_of_the_per_process_counters);
    RUN_TEST(tables_are_indexed_by_the_current_process);
    RUN_TEST(project_pointer_is_the_row_base);
    RUN_TEST(result_is_the_full_longword_from_eval_rights);
    RUN_TEST(status_pointer_is_passed_through);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
